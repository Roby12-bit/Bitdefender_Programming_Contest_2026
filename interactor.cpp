// interactor.cpp — "A tale of two suns" interactive judge
#include <bits/stdc++.h>
using namespace std;

// ------------------- CONSTANTS -------------------

enum Component {
    ENERGY = 0,
    WATER  = 1,
    DATA   = 2,
    RAM    = 3,
    GPU    = 4,
    NUM_COMP = 5
};

const int TRASH = 5; // hex that produces nothing
const int FIXED_N = 54;
const int FIXED_H = 19;

// building[v] = 0 means empty, 1+ means data center of that level
const int B_NONE = 0;

// costs
const int COST_DC[NUM_COMP] = {
    1, // ENERGY
    1, // WATER
    1, // DATA
    1, // RAM
    0  // GPU
};

const int COST_CABLE[NUM_COMP] = {
    1, // ENERGY
    1, // WATER
    0, // DATA
    0, // RAM
    0  // GPU
};

// UPGRADE_DC to level X costs: X RAM + (X+1) GPU (dynamic, no static array)

// ------------------- STRUCTURES -------------------

struct Hex {
    int component;         // 0..5 (5 = trash)
    int se;                // 2..12 (0 for trash)
    vector<int> nodes;     // adjacent locations
};

struct Edge {
    int u, v;
};

// ------------------- GLOBAL VARIABLES -------------------

int N, M, H, Z;
vector<Hex> hexes;
vector<Edge> edges;
map<pair<int,int>, int> edge_map;    // (min,max) -> edge index
vector<int> se_seq;                  // se_seq[1..Z] — solar energy per day

// Converters: indices 0..4 are C0..C4, index 5 is C31.
vector<array<bool, 6>> node_converters;
struct ConverterDef { int type; vector<int> nodes; }; // type: 0-4 or 31
vector<ConverterDef> converters;

vector<int> building;          // 0 = none, 1+ = data center level
vector<bool> has_cable;        // on sides
vector<long long> components(NUM_COMP, 0); // player's components
int points = 0;

vector<vector<int>> node_adj;        // neighbours (for distance rule)
vector<vector<int>> node_edges;      // incident sides per location

void derive_edges() {
    edge_map.clear();
    edges.clear();
    for (int i = 0; i < H; ++i) {
        for (int j = 0; j < (int)hexes[i].nodes.size(); ++j) {
            int u = hexes[i].nodes[j];
            int v = hexes[i].nodes[(j + 1) % hexes[i].nodes.size()];
            auto e = make_pair(min(u, v), max(u, v));
            if (edge_map.find(e) == edge_map.end()) {
                edge_map[e] = (int)edges.size();
                edges.push_back({e.first, e.second});
            }
        }
    }
    M = (int)edges.size();

    node_adj.assign(N, {});
    node_edges.assign(N, {});
    for (int i = 0; i < M; ++i) {
        node_adj[edges[i].u].push_back(edges[i].v);
        node_adj[edges[i].v].push_back(edges[i].u);
        node_edges[edges[i].u].push_back(i);
        node_edges[edges[i].v].push_back(i);
    }
}

// ------------------- HELPERS -------------------

[[noreturn]] void fail(const string &msg) {
    cerr << "Judge error: " << msg << "\n";
    cout << "SCORE: 0" << "\n";
    cout.flush();
    exit(1);
}

bool spend_cost(const int cost[NUM_COMP]) {
    for (int i = 0; i < NUM_COMP; ++i) {
        if (components[i] < cost[i]) return false;
    }
    for (int i = 0; i < NUM_COMP; ++i) {
        components[i] -= cost[i];
    }
    return true;
}

// Spend the dynamic cost for upgrading to level target_level:
// target_level RAM + (target_level + 1) GPU
bool spend_upgrade_cost(int target_level) {
    long long ram_needed = target_level;
    long long gpu_needed = target_level + 1;
    if (components[RAM] < ram_needed || components[GPU] < gpu_needed) return false;
    components[RAM] -= ram_needed;
    components[GPU] -= gpu_needed;
    return true;
}

// distance rule: no adjacent location may have a data center
bool check_distance_rule(int v) {
    if (building[v] != B_NONE) return false;
    for (int u : node_adj[v]) {
        if (building[u] != B_NONE) return false;
    }
    return true;
}

// data center must be connected to cable network
bool check_dc_connected_to_cable(int v) {
    for (int e : node_edges[v]) {
        if (has_cable[e]) return true;
    }
    return false;
}

// cable must be connected to existing network (cable or data center)
bool check_cable_connection(int e) {
    int u = edges[e].u;
    int v = edges[e].v;

    if (building[u] != B_NONE || building[v] != B_NONE) return true;

    for (int e2 : node_edges[u]) {
        if (has_cable[e2]) return true;
    }
    for (int e2 : node_edges[v]) {
        if (has_cable[e2]) return true;
    }
    return false;
}

// get the best conversion rate when giving away component give_type
int get_conversion_rate(int give_type) {
    for (int v = 0; v < N; ++v) {
        if (building[v] > B_NONE && node_converters[v][give_type]) return 2;
    }
    for (int v = 0; v < N; ++v) {
        if (building[v] > B_NONE && node_converters[v][5]) return 3;
    }
    return 4;
}

// produce components based on solar energy — each DC of level L produces L components
void produce_components(int se_value) {
    for (const auto &h : hexes) {
        if (h.component == TRASH) continue;
        if (h.se != se_value) continue;
        for (int v : h.nodes) {
            if (v < 0 || v >= N) continue;
            if (building[v] > B_NONE) {
                components[h.component] += building[v];
            }
        }
    }
}

// ------------------- MAIN -------------------

int main(int argc, char** argv) {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    if (argc < 2) {
        cerr << "Usage: interactor <input_file>\n";
        return 1;
    }

    ifstream fin(argv[1]);
    if (!fin) {
        cerr << "Cannot open input file: " << argv[1] << "\n";
        return 1;
    }

    int G;
    fin >> G;
    if (!fin || G <= 0) {
        cerr << "Invalid input file: missing blueprint count\n";
        return 1;
    }

    cout << G << "\n";
    cout.flush();

    int total_points = 0;
    for (int g = 0; g < G; ++g) {
        fin >> Z;
        if (!fin) {
            cerr << "Invalid input file header at blueprint " << g + 1 << "\n";
            return 1;
        }

        N = FIXED_N;
        H = FIXED_H;

        hexes.resize(H);
        for (int i = 0; i < H; ++i) {
            fin >> hexes[i].component >> hexes[i].se;
            hexes[i].nodes.resize(6);
            for (int j = 0; j < 6; ++j) {
                fin >> hexes[i].nodes[j];
            }
        }

        derive_edges();

        // Read converters (lines starting with 'C')
        node_converters.assign(N, {});
        converters.clear();
        string tok;
        while (fin >> tok) {
            if (tok[0] != 'C') {
                // Not a converter line — this is the first SE value
                se_seq.assign(Z + 1, 0);
                se_seq[1] = stoi(tok);
                break;
            }
            int ctype = stoi(tok.substr(1)); // 0-4 or 31
            ConverterDef cd;
            cd.type = ctype;
            int cnt;
            fin >> cnt;
            for (int i = 0; i < cnt; i++) {
                int v;
                fin >> v;
                cd.nodes.push_back(v);
                if (v >= 0 && v < N) {
                    if (ctype == 31) node_converters[v][5] = true;
                    else if (0 <= ctype && ctype < NUM_COMP) node_converters[v][ctype] = true;
                }
            }
            converters.push_back(cd);
        }

        for (int z = 2; z <= Z; ++z) {
            fin >> se_seq[z];
            if (!fin) {
                cerr << "Not enough SE values in input file\n";
                return 1;
            }
            if (se_seq[z] < 2 || se_seq[z] > 12) {
                cerr << "Invalid SE value " << se_seq[z]
                    << " at day " << z << "\n";
                return 1;
            }
        }

        // Initialize state
        building.assign(N, B_NONE);
        has_cable.assign(M, false);
        fill(components.begin(), components.end(), 0LL);
        points = 0;

        // ------------------- SEND BLUEPRINT TO PLAYER -------------------

        cout << Z << "\n";
        for (int i = 0; i < H; ++i) {
            cout << hexes[i].component << " " << hexes[i].se;
            for (int v : hexes[i].nodes) cout << " " << v;
            cout << "\n";
        }
        for (auto &cd : converters) {
            cout << "C" << cd.type << " " << cd.nodes.size();
            for (int v : cd.nodes) cout << " " << v;
            cout << "\n";
        }
        cout.flush();

        // ------------------- READ INITIAL PLACEMENTS -------------------
        bool start = false;

        for (int i = 0; i < 2; ++i) {
            string cmd;
            if (!(cin >> cmd)) {
                fail("Expected BUILD_DC for initial placement " + to_string(i+1) + ", got EOF");
            }
            if (i == 0 && cmd == "START") {
                if (start)
                    fail("START command sent more than once");
                start = true;
                i--;
                continue;
            }
            if (cmd != "BUILD_DC") {
                fail("Expected BUILD_DC for initial placement " + to_string(i+1) + ", got '" + cmd + "'");
            }
            int v;
            if (!(cin >> v)) {
                fail("Initial data center: failed to read location index");
            }
            if (v < 0 || v >= N) {
                fail("Initial data center: location index " + to_string(v) + " out of range [0, " + to_string(N) + ")");
            }
            if (!check_distance_rule(v)) {
                fail("Initial data center " + to_string(i+1) + " violates distance rule or location already occupied");
            }
            building[v] = 1; // level 1
            points += 1;
        }

        for (int i = 0; i < 2; ++i) {
            string cmd;
            if (!(cin >> cmd)) {
                fail("Expected BUILD_CABLE for initial placement " + to_string(i+1) + ", got EOF");
            }
            if (cmd != "BUILD_CABLE") {
                fail("Expected BUILD_CABLE for initial placement " + to_string(i+1) + ", got '" + cmd + "'");
            }
            int u, v;
            if (!(cin >> u >> v)) {
                fail("Initial cable: failed to read location indices");
            }
            auto key = make_pair(min(u, v), max(u, v));
            auto it = edge_map.find(key);
            if (it == edge_map.end()) {
                fail("Initial cable: non-existent side (" + to_string(u) + ", " + to_string(v) + ")");
            }
            int e = it->second;
            if (has_cable[e]) {
                fail("Initial cable " + to_string(i+1) + " on already occupied side (" + to_string(u) + ", " + to_string(v) + ")");
            }
            // cable must be adjacent to an initial data center
            if (building[u] == B_NONE && building[v] == B_NONE) {
                fail("Initial cable " + to_string(i+1) + " must be adjacent to an initial data center");
            }
            has_cable[e] = true;
        }

        // ------------------- DAY LOOP -------------------

        for (int z = 1; z <= Z; ++z) {
            int se = se_seq[z];
            produce_components(se);

            cout << se << "\n";
            for (int i = 0; i < NUM_COMP; ++i) {
                cout << components[i] << (i + 1 == NUM_COMP ? '\n' : ' ');
            }
            cout.flush();

            int K;
            if (!(cin >> K)) {
                fail("Failed to read number of actions K");
            }
            if (K < 0) {
                fail("Invalid K (number of actions in a day)");
            }

            for (int i = 0; i < K; ++i) {
                string cmd;
                if (!(cin >> cmd)) {
                    fail("EOF while reading actions");
                }

                if (cmd == "BUILD_DC") {
                    int v;
                    if (!(cin >> v)) {
                        fail("BUILD_DC: failed to read location");
                    }
                    if (v < 0 || v >= N) {
                        fail("BUILD_DC: location " + to_string(v) + " out of range");
                    }
                    if (building[v] != B_NONE) {
                        fail("BUILD_DC: location " + to_string(v) + " already occupied");
                    }
                    if (!check_distance_rule(v)) {
                        fail("BUILD_DC: location " + to_string(v) + " violates distance rule");
                    }
                    if (!check_dc_connected_to_cable(v)) {
                        fail("BUILD_DC: location " + to_string(v) + " not connected to cable network");
                    }
                    if (!spend_cost(COST_DC)) {
                        fail("BUILD_DC: not enough components");
                    }
                    building[v] = 1; // level 1
                    points += 1;

                } else if (cmd == "BUILD_CABLE") {
                    int u, v;
                    if (!(cin >> u >> v)) {
                        fail("BUILD_CABLE: failed to read location indices");
                    }
                    auto key = make_pair(min(u, v), max(u, v));
                    auto it = edge_map.find(key);
                    if (it == edge_map.end()) {
                        fail("BUILD_CABLE: non-existent side (" + to_string(u) + ", " + to_string(v) + ")");
                    }
                    int e = it->second;
                    if (has_cable[e]) {
                        fail("BUILD_CABLE: side (" + to_string(u) + ", " + to_string(v) + ") already occupied");
                    }
                    if (!check_cable_connection(e)) {
                        fail("BUILD_CABLE: side (" + to_string(u) + ", " + to_string(v) + ") not connected to player network");
                    }
                    if (!spend_cost(COST_CABLE)) {
                        fail("BUILD_CABLE: not enough components");
                    }
                    has_cable[e] = true;

                } else if (cmd == "CONVERT") {
                    int give_type, get_type;
                    if (!(cin >> give_type >> get_type)) {
                        fail("CONVERT: failed to read component types");
                    }
                    if (give_type < 0 || give_type >= NUM_COMP ||
                        get_type < 0 || get_type >= NUM_COMP) {
                        fail("CONVERT: invalid component type (give=" + to_string(give_type) + ", get=" + to_string(get_type) + ")");
                    }
                    if (give_type == get_type) {
                        fail("CONVERT: give_component == get_component == " + to_string(give_type));
                    }
                    int rate = get_conversion_rate(give_type);
                    if (components[give_type] < rate) {
                        fail("CONVERT: not enough components (have " + to_string(components[give_type]) + " of type " + to_string(give_type) + ", need " + to_string(rate) + ")");
                    }
                    components[give_type] -= rate;
                    components[get_type]  += 1;

                } else if (cmd == "UPGRADE_DC") {
                    int v;
                    if (!(cin >> v)) {
                        fail("UPGRADE_DC: failed to read location");
                    }
                    if (v < 0 || v >= N) {
                        fail("UPGRADE_DC: location " + to_string(v) + " out of range");
                    }
                    if (building[v] < 1) {
                        fail("UPGRADE_DC: no data center at location " + to_string(v));
                    }
                    int target_level = building[v] + 1;
                    if (!spend_upgrade_cost(target_level)) {
                        fail("UPGRADE_DC: not enough components for level " + to_string(target_level)
                            + " (need " + to_string(target_level) + " RAM + " + to_string(target_level + 1) + " GPU)");
                    }
                    building[v] = target_level;
                    points += 1; // each level is worth 1 point

                } else {
                    fail("Unknown command: '" + cmd + "'");
                }
            }
        }

        total_points += points;
    }

    fin.close();
    cout << "SCORE: " << total_points << "\n";
    cout.flush();
    return 0;
}
