#include <iostream>
#include <vector>
#include <array>
#include <string>
#include <set>
#include <queue>
#include <algorithm>
#include <cmath>

using namespace std;

// --- STRUCTURI DE BAZA ---
struct HexagonData {
    int id_component; 
    int SE;           
};

struct Node {
    vector<int> adj_nodes;
    vector<string> converters;
    vector<HexagonData> adj_hexes;
};

struct StartPositions {
    int u, v, score;
};

struct PathResult {
    int cost;
    vector<int> path;
};

struct ConversionOp {
    int resource_from, resource_to, amount_gained, exact_cost_rate;
};

enum ActionType { NONE, UPGRADE, BUILD };

struct TargetAction {
    ActionType type;
    int target_node;
    long long roi_num, roi_den;
};

// --- CLASE LOGICE ---
class Topology {
public:
    vector<Node> graph;
    Topology() { graph.resize(54); }

    void addHexagon(int id_comp, int SE, const vector<int>& vertices) {
        HexagonData hexData = {id_comp, SE};
        for (int i = 0; i < 6; ++i) {
            int u = vertices[i];
            int v = vertices[(i + 1) % 6];
            if (find(graph[u].adj_nodes.begin(), graph[u].adj_nodes.end(), v) == graph[u].adj_nodes.end())
                graph[u].adj_nodes.push_back(v);
            if (find(graph[v].adj_nodes.begin(), graph[v].adj_nodes.end(), u) == graph[v].adj_nodes.end())
                graph[v].adj_nodes.push_back(u);
            graph[u].adj_hexes.push_back(hexData);
        }
    }

    void addConverter(const string& convType, const vector<int>& nodes) {
        for (int node : nodes) {
            if (node >= 0 && node < 54)
                graph[node].converters.push_back(convType);
        }
    }
};

class ExpectedProduction {
    const int prob_x36[13] = {0, 0, 1, 2, 3, 4, 5, 6, 5, 4, 3, 2, 1};
public:
    vector<array<int, 5>> calculateAllNodes(const vector<Node>& graph) {
        vector<array<int, 5>> all_nodes_expected(graph.size());
        for (size_t i = 0; i < graph.size(); ++i) {
            array<int, 5> res = {0, 0, 0, 0, 0};
            for (const auto& hex : graph[i].adj_hexes) {
                if (hex.id_component >= 0 && hex.id_component <= 4 && hex.SE >= 2 && hex.SE <= 12) {
                    res[hex.id_component] += prob_x36[hex.SE];
                }
            }
            all_nodes_expected[i] = res;
        }
        return all_nodes_expected;
    }
};

class BootstrapSelector {
public:
    StartPositions getBestInitialDCs(
        const vector<Node>& graph,
        const vector<array<int, 5>>& expected_gen)
    {
        StartPositions best = {-1, -1, -1000000000};

        for (int u = 0; u < 54; ++u) {
            for (int v = u + 1; v < 54; ++v) {
                // Nu pot fi adiacente
                bool is_adj = false;
                for (int nb : graph[u].adj_nodes) {
                    if (nb == v) { is_adj = true; break; }
                }
                if (is_adj) continue;

                int score = 0;
                int total[5];

                for (int r = 0; r < 5; ++r) {
                    total[r] = expected_gen[u][r] + expected_gen[v][r];
                }

                // Ponderi producție optimizate
                for (int r = 0; r < 5; ++r) {
                    score += total[r] * (r < 3 ? 7 : 9);
                }

                // Evită lipsa completă a resurselor critice
                for (int r = 0; r < 5; ++r) {
                    if (total[r] == 0)
                        score -= (r >= 3 ? 12000 : 8000);
                }

                // Sinergie între locații
                for (int r = 0; r < 5; ++r) {
                    if (expected_gen[u][r] > 0 && expected_gen[v][r] > 0)
                        score += 150;
                }

                // Convertoare specializate
                for (const string& c : graph[u].converters) {
                    if (c == "C3" || c == "C4") score += 350;
                    else if (c == "C31") score += 150;
                    else score += 50;
                }

                for (const string& c : graph[v].converters) {
                    if (c == "C3" || c == "C4") score += 350;
                    else if (c == "C31") score += 150;
                    else score += 50;
                }

                int distinct = 0;
                for (int r = 0; r < 5; ++r)
                    if (total[r] > 0) distinct++;

                score += distinct * 600;

                if (score > best.score)
                    best = {u, v, score};
            }
        }

        return best;
    }
};

struct GlobalBFSResult {
    vector<int> dist;
    vector<int> parent;
};

class Pathfinding {
public:
    GlobalBFSResult runGlobalBFS(
        const vector<Node>& graph,
        const set<int>& existing_network)
    {
        int n = graph.size();
        vector<int> dist(n, -1);
        vector<int> parent(n, -1);
        queue<int> q;

        for (int node : existing_network) {
            dist[node] = 0;
            q.push(node);
        }

        while (!q.empty()) {
            int curr = q.front();
            q.pop();

            for (int neighbor : graph[curr].adj_nodes) {
                if (dist[neighbor] == -1) {
                    dist[neighbor] = dist[curr] + 1;
                    parent[neighbor] = curr;
                    q.push(neighbor);
                }
            }
        }

        return {dist, parent};
    }

    PathResult getPathTo(
        int target_node,
        const GlobalBFSResult& bfs_data)
    {
        if (bfs_data.dist[target_node] == -1)
            return {-1, {}};

        if (bfs_data.dist[target_node] == 0)
            return {0, {}};

        vector<int> path;
        int step = target_node;

        while (bfs_data.dist[step] > 0) {
            path.push_back(step);
            step = bfs_data.parent[step];
        }
        path.push_back(step);
        reverse(path.begin(), path.end());

        return {bfs_data.dist[target_node], path};
    }
};

class TransactionOptimizer {
public:
    int getExactCost(int src, const array<bool, 5>& has_spec, bool has_gen) const {
        if (has_spec[src]) return 2;
        if (has_gen) return 3;
        return 4;
    }

    // Planificator exact de conversie care garantează plata costului
    bool planConversions(
        const array<int, 5>& current_res,
        const array<int, 5>& target_cost,
        const array<bool, 5>& has_spec,
        bool has_gen,
        bool is_upgrade,
        vector<ConversionOp>& plan,
        array<int, 5>& remaining_res) const
    {
        plan.clear();
        array<int, 5> stock = current_res;

        for (int req = 0; req < 5; ++req) {
            while (stock[req] < target_cost[req]) {
                int need = target_cost[req] - stock[req];
                int best_src = -1;
                int best_cost = 10;
                int best_surplus = -1;

                for (int src = 0; src < 5; ++src) {
                    if (src == req) continue;
                    int surplus = stock[src] - target_cost[src];
                    int cost = getExactCost(src, has_spec, has_gen);

                    if (surplus >= cost) {
                        // Resursele inutile pentru acțiune au prioritate de sacrificiu
                        bool non_essential = is_upgrade ? (src < 3) : (src == 4);

                        if (cost < best_cost || 
                           (cost == best_cost && non_essential) ||
                           (cost == best_cost && surplus > best_surplus)) {
                            best_cost = cost;
                            best_surplus = surplus;
                            best_src = src;
                        }
                    }
                }

                if (best_src == -1) return false;

                int max_units = (stock[best_src] - target_cost[best_src]) / best_cost;
                int transfer = min(need, max_units);
                if (transfer <= 0) return false;

                stock[best_src] -= transfer * best_cost;
                stock[req] += transfer;

                if (!plan.empty() && plan.back().resource_from == best_src && plan.back().resource_to == req) {
                    plan.back().amount_gained += transfer;
                } else {
                    plan.push_back({best_src, req, transfer, best_cost});
                }
            }
        }

        for (int i = 0; i < 5; ++i) {
            if (stock[i] < target_cost[i]) return false;
            remaining_res[i] = stock[i] - target_cost[i];
        }
        return true;
    }
};

class ActionPlanner {
    using Res = array<int, 5>;
    TransactionOptimizer opt;

    array<long long, 5> currentDailyProduction(
        const vector<int>& existing_dcs,
        const vector<int>& dc_levels,
        const vector<array<int, 5>>& expected_gen) const
    {
        array<long long, 5> p = {0, 0, 0, 0, 0};
        for (int dc : existing_dcs) {
            int level = dc_levels[dc];
            for (int r = 0; r < 5; ++r) {
                p[r] += (long long)expected_gen[dc][r] * level;
            }
        }
        return p;
    }

    int daysToAfford(
        const Res& current,
        const Res& cost,
        const array<long long, 5>& daily,
        int max_days,
        const array<bool, 5>& has_spec,
        bool has_generic,
        bool is_upgrade) const
    {
        vector<ConversionOp> dummy_plan;
        Res dummy_rem;

        for (int d = 0; d <= max_days; ++d) {
            Res projected = current;
            for (int r = 0; r < 5; ++r) {
                projected[r] += (int)((daily[r] * d) / 36LL);
            }
            if (opt.planConversions(projected, cost, has_spec, has_generic, is_upgrade, dummy_plan, dummy_rem))
                return d;
        }
        return max_days + 1;
    }

    int futureUpgradeCount(
        const Res& stock,
        const vector<int>& dcs,
        const vector<int>& levels) const
    {
        int count = 0;
        for (int dc : dcs) {
            int L = levels[dc];
            if (stock[3] >= L + 1 && stock[4] >= L + 2) {
                count++;
            }
        }
        return count;
    }

    long long bufferPenalty(
        const Res& stock,
        const vector<int>& dcs,
        const vector<int>& levels) const
    {
        if (dcs.empty()) return 0;
        int min_ram = 1e9;
        int min_gpu = 1e9;

        for (int dc : dcs) {
            int L = levels[dc];
            min_ram = min(min_ram, L + 1);
            min_gpu = min(min_gpu, L + 2);
        }

        long long penalty = 0;
        if (stock[3] < min_ram)
            penalty += (min_ram - stock[3]) * 15000LL;
        if (stock[4] < min_gpu)
            penalty += (min_gpu - stock[4]) * 15000LL;

        return penalty;
    }

public:
    TargetAction getBestAction(
        int z,
        int Z,
        const vector<int>& existing_dcs,
        const vector<int>& valid_nodes,
        const vector<int>& dc_levels,
        const vector<array<int, 5>>& expected_gen,
        const vector<int>& cable_costs,
        const array<int, 5>& current_res,
        const array<bool, 5>& has_spec,
        bool has_generic)
    {
        TargetAction best = {NONE, -1, -1, 1};
        int days_left = Z - z;
        if (days_left <= 0) return best;

        array<long long, 5> daily = currentDailyProduction(existing_dcs, dc_levels, expected_gen);
        const int LOOKAHEAD = min(2, days_left - 1);

        // ============================================================
        // 1. EVALUARE UPGRADE (Motorul principal de punctaj și producție)
        // ============================================================
        for (int dc : existing_dcs) {
            int L = dc_levels[dc];
            Res cost = {0, 0, 0, L + 1, L + 2};

            int wait = daysToAfford(current_res, cost, daily, min(days_left, 3), has_spec, has_generic, true);
            if (wait > 0) continue; // evaluăm doar acțiuni imediat executabile azi

            vector<ConversionOp> plan;
            Res after_payment;
            if (!opt.planConversions(current_res, cost, has_spec, has_generic, true, plan, after_payment))
                continue;

            Res future = after_payment;
            for (int r = 0; r < 5; ++r) {
                long long base = (daily[r] * LOOKAHEAD) / 36LL;
                long long extra = ((long long)expected_gen[dc][r] * LOOKAHEAD) / 36LL;
                future[r] += (int)(base + extra);
            }

            int future_upgrades = futureUpgradeCount(future, existing_dcs, dc_levels);
            long long buffer = bufferPenalty(future, existing_dcs, dc_levels);

            long long growth_exact = 0;
            for (int r = 0; r < 5; ++r) {
                int deficit = 0;
                if (r == 0) deficit = max(0, 2 - current_res[0]);
                if (r == 1) deficit = max(0, 2 - current_res[1]);
                if (r == 2) deficit = max(0, 2 - current_res[2]);
                if (r == 3) deficit = max(0, (L + 2) - current_res[3]);
                if (r == 4) deficit = max(0, (L + 3) - current_res[4]);

                long long relevance = deficit > 0 ? 7 : 1; 
                growth_exact += (long long)expected_gen[dc][r] * relevance;
            }

            long long value = 1000000LL + (growth_exact * max(0, days_left - 1) * 16000LL) / 72LL;
            value += future_upgrades * 130000LL;
            value -= buffer;

            // Pe final de joc (Z-2..Z), forțăm upgrade-ul cel mai ieftin
            if (days_left <= 2) {
                value = 5500000LL - (L * 8000LL);
            }

            long long score_num = value;
            long long score_den = 1;

            if (best.type == NONE || score_num * best.roi_den > best.roi_num * score_den) {
                best = {UPGRADE, dc, score_num, score_den};
            }
        }

        // ============================================================
        // 2. EVALUARE BUILD DC (Extindere către fabrici noi)
        // ============================================================
        for (int node : valid_nodes) {
            int cable = cable_costs[node];
            if (cable < 0) continue;

            Res cost = {1 + cable, 1 + cable, 1, 1, 0};

            int wait = daysToAfford(current_res, cost, daily, min(days_left, 3), has_spec, has_generic, false);
            if (wait > 0) continue;

            vector<ConversionOp> plan;
            Res after_payment;
            if (!opt.planConversions(current_res, cost, has_spec, has_generic, false, plan, after_payment))
                continue;

            Res future = after_payment;
            for (int r = 0; r < 5; ++r) {
                long long base = (daily[r] * LOOKAHEAD) / 36LL;
                long long new_dc = ((long long)expected_gen[node][r] * LOOKAHEAD) / 36LL;
                future[r] += (int)(base + new_dc);
            }

            vector<int> future_dcs = existing_dcs;
            vector<int> future_levels = dc_levels;
            future_dcs.push_back(node);
            future_levels[node] = 1;

            int future_upgrades = futureUpgradeCount(future, future_dcs, future_levels);
            long long buffer = bufferPenalty(future, future_dcs, future_levels);

            long long growth_exact = 0;
            for (int r = 0; r < 5; ++r) {
                int deficit = max(0, 3 - current_res[r]);
                long long relevance = deficit > 0 ? 7 : 1;
                growth_exact += (long long)expected_gen[node][r] * relevance;
            }

            long long value = 1000000LL + (growth_exact * max(0, days_left - 1) * 16000LL) / 72LL;
            value += future_upgrades * 130000LL;
            value -= buffer;
            value -= (long long)cable * 18000LL;

            if (days_left <= 2) {
                value = 4500000LL - ((long long)cable * 50000LL);
            }

            long long score_num = value;
            long long score_den = 1;

            if (best.type == NONE || score_num * best.roi_den > best.roi_num * score_den) {
                best = {BUILD, node, score_num, score_den};
            }
        }

        return best;
    }
};

// --- AGENTUL PRINCIPAL INTERACTIV ---
class AGI_Agent {
    Topology topo;
    ExpectedProduction prod;
    BootstrapSelector boot;
    Pathfinding pathFinder;
    ActionPlanner planner;
    TransactionOptimizer optimizer;

    array<bool, 5> has_spec_conv = {false, false, false, false, false};
    bool has_gen_conv = false;

    void updateConverters(int node) {
        for (const string& conv : topo.graph[node].converters) {
            if (conv == "C31") has_gen_conv = true;
            else if (conv == "C0") has_spec_conv[0] = true;
            else if (conv == "C1") has_spec_conv[1] = true;
            else if (conv == "C2") has_spec_conv[2] = true;
            else if (conv == "C3") has_spec_conv[3] = true;
            else if (conv == "C4") has_spec_conv[4] = true;
        }
    }
    
    vector<array<int, 5>> expected_gen;
    vector<int> existing_dcs;
    vector<int> dc_levels;
    set<int> existing_network;

public:
    void run() {
        int Z;
        if (!(cin >> Z)) return;

        // 1. Parsare Blueprint (19 hexagoane)
        for (int i = 0; i < 19; ++i) {
            int id_comp, SE;
            vector<int> v(6);
            cin >> id_comp >> SE >> v[0] >> v[1] >> v[2] >> v[3] >> v[4] >> v[5];
            topo.addHexagon(id_comp, SE, v);
        }

        // 2. Parsare Convertoare (6 linii)
        string cx; int count;
        for (int i = 0; i < 6; ++i) {
            cin >> cx >> count;
            vector<int> nodes(count);
            for (int j = 0; j < count; ++j) cin >> nodes[j];
            topo.addConverter(cx, nodes);
        }

        // 3. Setup Bootstrap
        expected_gen = prod.calculateAllNodes(topo.graph);
        StartPositions start = boot.getBestInitialDCs(topo.graph, expected_gen);
        
        dc_levels.assign(54, 0);
        dc_levels[start.u] = 1;
        dc_levels[start.v] = 1;
        existing_dcs.push_back(start.u);
        existing_dcs.push_back(start.v);
        
        existing_network.insert(start.u);
        existing_network.insert(start.v);

        // Alegem vecinii direcționați optim către convertoare sau producție
        auto pickBestAdj = [&](int root, int other) {
            int best_target = -1;
            long long best_score = -1;
            for (int nb : topo.graph[root].adj_nodes) {
                if (nb == other) continue;
                long long cand = 0;
                for (int s : topo.graph[nb].adj_nodes) {
                    if (s != root && s != other) {
                        for (int r = 0; r < 5; ++r) cand += expected_gen[s][r] * (r >= 3 ? 3 : 2);
                    }
                }
                for (const string& c : topo.graph[nb].converters) {
                    if (c == "C3" || c == "C4") cand += 300;
                    else if (c == "C31") cand += 150;
                }
                if (cand > best_score) {
                    best_score = cand;
                    best_target = nb;
                }
            }
            return best_target != -1 ? best_target : topo.graph[root].adj_nodes[0];
        };

        int adj_u = pickBestAdj(start.u, start.v);
        int adj_v = pickBestAdj(start.v, start.u);
        
        existing_network.insert(adj_u);
        existing_network.insert(adj_v);

        cout << "START\n" << flush;
        cout << "BUILD_DC " << start.u << "\n";
        cout << "BUILD_DC " << start.v << "\n";
        cout << "BUILD_CABLE " << start.u << " " << adj_u << "\n";
        cout << "BUILD_CABLE " << start.v << " " << adj_v << "\n";
        cout << flush;

        updateConverters(start.u);
        updateConverters(start.v);

        // 4. Bucla Zilnică Interactivă
        for (int z = 0; z < Z; ++z) {
            int SE;
            array<int, 5> res;
            cin >> SE >> res[0] >> res[1] >> res[2] >> res[3] >> res[4];

            vector<string> day_cmds;

            // Buclă Multi-Action: se execută greedy toate acțiunile de valoare mare din ziua curentă
            int actions_taken = 0;
            while (actions_taken < 15) {
                actions_taken++;
                vector<int> valid_nodes;
                for (int i = 0; i < 54; ++i) {
                    if (dc_levels[i] > 0) continue;
                    bool has_adj_dc = false;
                    for (int neighbor : topo.graph[i].adj_nodes) {
                        if (dc_levels[neighbor] > 0) has_adj_dc = true;
                    }
                    if (!has_adj_dc) valid_nodes.push_back(i);
                }

                GlobalBFSResult bfs_data = pathFinder.runGlobalBFS(topo.graph, existing_network);
                
                vector<int> cable_costs(54, -1);
                for (int node : valid_nodes) {
                    cable_costs[node] = bfs_data.dist[node];
                }

                TargetAction target = planner.getBestAction(
                    z,
                    Z,
                    existing_dcs,
                    valid_nodes,
                    dc_levels,
                    expected_gen,
                    cable_costs,
                    res,
                    has_spec_conv,
                    has_gen_conv
                );

                if (target.type == NONE) break;

                array<int, 5> target_cost = {0, 0, 0, 0, 0};
                PathResult pr;
                
                if (target.type == BUILD) {
                    pr = pathFinder.getPathTo(target.target_node, bfs_data);
                    if (pr.cost < 0) break; // securitate de conectivitate
                    target_cost = {1 + pr.cost, 1 + pr.cost, 1, 1, 0};
                } else {
                    int L = dc_levels[target.target_node];
                    target_cost = {0, 0, 0, L + 1, L + 2};
                }

                vector<ConversionOp> plan;
                array<int, 5> next_res;
                if (!optimizer.planConversions(res, target_cost, has_spec_conv, has_gen_conv, target.type == UPGRADE, plan, next_res)) {
                    break;
                }

                // Emitem conversiile
                for (const auto& op : plan) {
                    for (int k = 0; k < op.amount_gained; ++k) {
                        day_cmds.push_back("CONVERT " + to_string(op.resource_from) + " " + to_string(op.resource_to));
                    }
                }

                // Emitem acțiunea
                if (target.type == BUILD) {
                    if (pr.path.size() >= 2) {
                        for (size_t i = 0; i < pr.path.size() - 1; ++i) {
                            day_cmds.push_back("BUILD_CABLE " + to_string(pr.path[i]) + " " + to_string(pr.path[i+1]));
                            existing_network.insert(pr.path[i]);
                            existing_network.insert(pr.path[i+1]);
                        }
                    }
                    day_cmds.push_back("BUILD_DC " + to_string(target.target_node));
                    dc_levels[target.target_node] = 1;
                    existing_dcs.push_back(target.target_node);
                    existing_network.insert(target.target_node);
                    updateConverters(target.target_node);
                } else {
                    day_cmds.push_back("UPGRADE_DC " + to_string(target.target_node));
                    dc_levels[target.target_node]++;
                }

                // Actualizăm stocul de resurse pentru continuarea buclei în aceeași zi
                res = next_res;
            }

            // Output format: K comenzi
            cout << day_cmds.size() << "\n";
            for (const string& cmd : day_cmds) {
                cout << cmd << "\n";
            }
            cout << flush;
        }
    }
};

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);

    int B;
    if (cin >> B) {
        while (B--) {
            AGI_Agent agent;
            agent.run();
        }
    }
    return 0;
}