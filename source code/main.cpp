/*
This is FROG Framework's optimizer source code.
FROG: "Fast Response to Optimization Goals for HTTP Adaptive Streaming over SDN" is an academic research study which optimize QoE for HTTP Adaptive Streaming (HAS) videos. This repository accompanies the study presenting FROG — a novel framework that integrates Software-Defined Networking (SDN), Common Media Client/Server Data (CMCD/SD), and optimization techniques based on Linear Programming (LP) and Mixed-Integer Linear Programming (MILP).

The framework addresses key challenges in HTTP Adaptive Streaming (HAS), including:

Multi-server and multi-path utilization
Fair and efficient bandwidth allocation
Dynamic path selection
Quality oscillation reduction
Stall minimization under varying network conditions
FROG employs a decomposition strategy, where the LP model determines feasible bandwidth allocations and flow paths, which then guide the MILP-based quality selection process.
Experimental results from an emulated testbed demonstrate that FROG achieves near-optimal Quality of Experience (QoE) with low latency and scales efficiently to 4,000 clients with approximately one-second optimization time. These results highlight the framework’s suitability for real-time, large-scale deployments.
Because the study covers a broad evaluation scope, this repository provides selected results and comparisons to ensure transparency and reproducibility. In addition, the optimization code used in the study is included here for research and reference purposes.
*/

#include <jsoncpp/json/json.h>
#include "cpr/include/cpr/cpr.h"
#include <iostream>
#include <string>
// #include <sstream>
#include "ilcplex/ilocplex.h"
ILOSTLBEGIN
#include <mutex> // For std::unique_lock
// #include <shared_mutex>
#include <thread>
#include <filesystem>
#include <unordered_map>
#include <map>
#include <set>
#include <optional>
#include <queue>
#include <unordered_set>
#include <algorithm>
#include <boost/asio.hpp>
#include <limits>

typedef IloArray<IloArray<IloIntVarArray>> IloIntVarArray3;
typedef IloArray<IloArray<IloArray<IloIntVarArray>>> IloIntVarArray4;
typedef IloArray<IloNumVarArray> IloNumVarArray2;
typedef IloArray<IloNumVarArray2> IloNumVarArray3;
typedef IloArray<IloNumVarArray3> IloNumVarArray4;
typedef vector<vector<double>> vector2d;
namespace fs = std::filesystem;
namespace io = boost::asio;

// Global variables of Optimization Formula
// Total Video Quality till this time slot. After each optimization cycle, this variable will be update as lambda_bar_c["Client IP"] += RESULT_OF_QUALITY_OPTIMIZATION.
vector<int> lambda_bar_c;
// vector<int> phi_c;
//  previous video quality opt result. Updated after each optimization cycle.
vector<int> l_bar_c;

// Total Intensity of Video Quality Switches.
// mu_bar_c["CLİENT IP"] += ABSOLUTE(CURRENT_OPT_RESULT - l_bar_c["CLİENT IP"])
// pre_video_quality["CLİENT IP"] = CURRENT_OPT_RESULT
vector<int> mu_bar_c; // total intensity of video qualtiy swithes

// total video quality isolation till now. 
vector<int> v_bar_c; 

//Used to get results
vector<std::chrono::duration<double>> optimizer_runtimes;
vector<std::chrono::duration<double>> flow_assignment_runtimes;
vector<vector<int>> video_quality;

//Used to keep network topology information and related opt variables and constants
class Net_Topo
{
public:
    Net_Topo()
    {
        requests_qty = 5000;// Total client number. we can't scale up to 5000 client, but 4000 client were good to go in the test with the automated requests not with the mininet simulation. 50 client were OK with mininet simulation.  
        srv_qty = servers.size();
        optimizer_qty = optimizers.size();
        hosts_qty = requests_qty + srv_qty + optimizer_qty;
        client_qty = hosts_qty - (srv_qty + optimizer_qty);
        sw_qty = 6;
        vertex_qty = hosts_qty + sw_qty - optimizer_qty;

        for (int i = 0; i < requests_qty; i++)
        {
            lambda_bar_c.emplace_back(0);
            l_bar_c.emplace_back(0);
            mu_bar_c.emplace_back(0);
            v_bar_c.emplace_back(0);
        }
        for (int i = 0; i < vertex_qty; ++i)
        {
            e.emplace_back(std::vector<int>(vertex_qty, 0));
            // link_capacity.emplace_back(std::vector<int>(vertex_qty, 0));
            link_capacity.emplace_back(std::vector<int>(vertex_qty, 0));
            b_ij.emplace_back(std::vector<int>(vertex_qty, 0));
        }

        for (int i = 0; i < srv_qty + sw_qty; ++i)
        {
            e.emplace_back(std::vector<int>(srv_qty + sw_qty, 0));
        }

        for (int i = 0; i < srv_qty + sw_qty; ++i) // No need client and server connedted ports so sws hold the connections. But servers are included due to correlation of index numbers between e array and this array
        {
            ports.emplace_back(std::vector<int>(vertex_qty));
        }
        /*
            cout << "2D Empty Edge Vector"
                 << "\n";
            for (auto v_array : e)
            {
                for (auto v : v_array)
                {
                    cout << v << " ";
                }
                cout << "\n";
            }
        */
        // get_sws_onos();
        // get_hosts_onos(); // gets client and servers from onos and updates e array - clients_index_update();
        set_x_y_sws(); // assings X and Y, e ids to ServerSideOFSWs and ClientSideOFSWs sets
        set_e_index();

        set_OF_SWs();          // All sws but Y
        set_OF_SWs_No_SSSWs(); // All sws but X and Y
        set_C_OF_SWs_Connections();

        set_Server_OF_SWs_Connections();
        set_ServerSideOFSWs_Connected_Servers();
        
        //Edge capacities. Since ONOS doesn't provide exact BW, we provide them manually.
        // std::vector<int> bandwith = {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100};
        // std::vector<int> bandwith = {1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000};
        // std::vector<int> bandwith = {5000, 5000, 5000, 5000, 5000, 5000, 5000, 5000, 5000, 5000, 5000};
        // std::vector<int> bandwith = {10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000};
        //std::vector<int> bandwith = {20000, 20000, 20000, 20000, 20000, 20000, 20000, 20000, 20000, 20000, 20000};//4000 clients
        std::vector<int> bandwith = {25000, 25000, 25000, 25000, 25000, 25000, 25000, 25000, 25000, 25000, 25000};//5000 clients
        //std::vector<int> bandwith = {37500, 37500, 37500, 37500, 37500, 37500, 37500, 37500, 37500, 37500, 37500}; // 7500 clients
        // std::vector<int> bandwith = {40000, 40000, 40000, 40000, 40000, 40000, 40000, 40000, 40000, 40000, 40000}; //8000 clients
        // std::vector<int> bandwith = {42500, 42500, 42500, 42500, 42500, 42500, 42500, 42500, 42500, 42500, 42500}; //8500 clients
        // std::vector<int> bandwith = {45000, 45000, 45000, 45000, 45000, 45000, 45000, 45000, 45000, 45000, 45000}; //9000 clients
        // std::vector<int> bandwith = {50000, 50000, 50000, 50000, 50000, 50000, 50000, 50000, 50000, 50000, 50000}; //10000 clients
        set_link_capacity(bandwith);
        set_b_ij();
    }; // end of Net_Topo Constructor

    int vertex_qty;
    int srv_qty;
    int client_qty;
    int sw_qty;
    int optimizer_qty;
    int hosts_qty;
    int requests_qty;
    std::vector<int> sws; // Contains sws IDs

    vector<vector<int>> b_ij; // avaiable bw
    std::vector<std::vector<std::vector<int>>> avg_b_ij;
    std::vector<std::vector<int>> pre_bytesSent;

    int avg_b_ij_index = 0;
    // vector<vector<int>> total_b_bar_cl_at_t_1_on_ij; // used bw at t-1 time period.
    // vector<vector<int>> avg_bw_usage;                // used to calculate avarage bw usage on edges
    int calls_of_set_b_ij = 0;
    // vector<vector<int>> link_capacity;
    vector<vector<int>> link_capacity;
    map<string, int> swPort_con_sw_e_index; // sw_id+port and connected sw at that port's e index are mapped
    map<string, int> host_con_swPort_host_e_index;
    map<string, int> sw_id_e_index; // holds sw id and it's e index
    map<int, string> sw_e_index_id; // holds sw e index and it's id
    // ALL OF_SWs - CLIENT SIDE OF_SWs vector
    bool i_j_equal = false;
    vector<int> OF_SWs;                             // All OFSWs except client side OFSWs
    vector<int> OF_SWs_No_SSSWs;                    // All OFSWs except client side and server side OFSWs
    map<int, set<int>> OF_SWs_Connections;          // All OFSWs except client side OFSWs and their connections index in e
    map<int, set<int>> OF_SWs_No_SSSWs_Connections; // All OFSWs except client side and server side OFSWs and their connections index in e
    map<int, set<int>> C_OF_SWs_Connections;        // Client side ofsw connections to other ofsws
    map<int, set<int>> Server_OF_SWs_Connections;   // Client side ofsw connections to other ofsws
    // map<int, set<int>> ServerSideOFSWs_Connected_Servers;
    map<string, int> clients_e_index; // client ip addresses and their index in e
    // map<string, int> server_e_index;
    set<string> servers = {"10.0.0.200"}; // server ip addresses
    // set<string> servers = {"10.0.0.201"}; // server ip addresses
    // set<string> servers = {"10.0.0.200", "10.0.0.201"}; // server ip addresses

    // set<string> servers = {"10.0.0.200", "10.0.0.201", "10.0.0.202", "10.0.0.203"}; // server ip addresses
    set<string> optimizers = {"10.0.0.100"};              // server ip addresses
    map<string, set<int>> srv_ip_con_sws_e_index;         // holds server ip and connected switchs e index
    map<int, set<int>> ServerSideOFSWs_Connected_Servers; // Server side OFSWs and connected server set index in e
    map<int, set<int>> N_i;                               // Client side OFSWs and connected client set index in e
    map<int, string> srv_e_index_ip;                      // holds server e index and corresponding ip address
    map<int, set<int>> srv_con_sws_e_index;               // holds server and connected switchs e index
    map<int, int> srv_con_sws_e_index2;                   // holds server and connected switch e index
    map<int, map<int, int>> srv_con_sw_port_e_index;      // holds servers e index and connected sw e index and sw's port number
    map<int, int> client_con_sw_e_index;                  // holds client and connected switchs e index
    map<string, int> client_ip_con_sw_e_index;            // holds client ip and connected switchs e index
    // set<int> C;                                      // client side OFSWs index in e
    set<int> ServerSideOFSWs; // server side OFSWs index in e
    set<int> ClientSideOFSWs; // client side OFSWs index in e

    vector<vector<int>> e; //Holds connections in a 2D array
    // vector<vector<int>> e(vertex_qty, vector<int>(vertex_qty, 0));
    std::vector<std::vector<int>> ports; // holds sws connection ports to other devices

    void set_e_index()
    {
        e = {
            // srv1     s1 s2 s3 s4 s5 s6
            {0, 1, 0, 0, 0, 0, 0}, // srv1
            {1, 0, 1, 1, 1, 0, 0}, // s1
            {0, 1, 0, 0, 1, 1, 0}, // s2
            {0, 1, 0, 0, 1, 1, 1}, // s3
            {0, 1, 1, 1, 0, 1, 1}, // s4
            {0, 0, 1, 1, 1, 0, 1}, // s5
            {0, 0, 0, 1, 1, 1, 0}  // s6
        };

        e.resize(vertex_qty); // Resize to 4 rows
        for (auto &row : e)
        {
            row.resize(vertex_qty, 0); // Fill new elements with value 1
        }

        for (int i = srv_qty + sw_qty; i < vertex_qty; i++)
        {
            for (int j = srv_qty + sw_qty; j < vertex_qty; j++)
            {
                e[i][j] = 0;
            }
        }
        // client connection capacity - ClientSideOFSWs
        for (int i = srv_qty + sw_qty; i < vertex_qty; i++)
        {
            for (auto j : ClientSideOFSWs)
            {
                // cout <<    "setting client's e index - client(i) " << i << " - sw(j) " << j <<"\n";
                e[i][j] = 1;
                e[j][i] = 1;
            }
        }

        /*
        cout << "e index\n";
        for (int i = 0; i < vertex_qty; i++)
        {
            for (int j = 0; j < vertex_qty; j++)
            {
                cout << e[i][j] << "\t";
            }
            cout << "\n";
        }
        cout << "\n";
        */
        /*
        e = {
    // srv1     s1 s2 s3 s4 s5 s6 c1 c2 c3 c4
            {0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // srv1
            {1, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0}, // s1
            {0, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0}, // s2
            {0, 1, 0, 0, 1, 1, 1, 0, 0, 0, 0}, // s3
            {0, 1, 1, 1, 0, 1, 1, 0, 0, 0, 0}, // s4
            {0, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0}, // s5
            {0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 1}, // s6
            {0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0}, // c1
            {0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0}, // c2
            {0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0}, // c3
            {0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0}  // c4
        };
        */
    } // end of set_e_index func

    // sets srv site and client site sws
    void set_x_y_sws()
    {
        ServerSideOFSWs.emplace(srv_qty);
        ClientSideOFSWs.emplace(srv_qty + sw_qty - 1);
    }

    void set_link_capacity(std::vector<int> bw /*Link capacities*/)
    {

        // int toByte = 1000 * 1000 / 8; // Byte per sec
        int toByte = 1; // Byte per sec

        // cout << "e size:" << e.size() << "\n";
        int sc_bw = 1000; // server and clients connection capacity

        int k = 0;
        // switch connection capacity
        for (int i = srv_qty; i < srv_qty + sw_qty - 1; i++)
        {
            for (int j = i + 1; j < srv_qty + sw_qty; j++)
            {
                if (e[i][j] == 1)
                {
                    link_capacity[i][j] = e[i][j] * bw[k] * toByte;
                    link_capacity[j][i] = e[j][i] * bw[k] * toByte;
                    k++;
                }
            }
        }

        // server connection capacity
        for (int i = 0; i < srv_qty; i++)
        {
            for (int j = srv_qty; j < srv_qty + sw_qty; j++)
            {
                link_capacity[i][j] = e[i][j] * sc_bw * toByte;
                link_capacity[j][i] = e[j][i] * sc_bw * toByte;
            }
        }

        // client connection capacity
        for (int i = srv_qty + sw_qty; i < vertex_qty; i++)
        {
            for (auto j : ClientSideOFSWs)
            {
                link_capacity[i][j] = sc_bw * toByte;
                link_capacity[j][i] = sc_bw * toByte;
            }
            /*
            for (int j = srv_qty; j < srv_qty + sw_qty; j++)
            {
                link_capacity[i][j] = e[i][j] * sc_bw * toByte;
                link_capacity[j][i] = e[j][i] * sc_bw * toByte;
            }
            */
        }

        /*
                link_capacity = {
                //srv1    s1   s2   s3   s4   s5   s6   c1   c2  c3  c4
                    {0,   100, 0,   0,   0,   0,   0,   0,   0,  0,  0}, //srv1
                    {100, 0,   100, 100, 100, 0,   0,   0,   0,  0,  0}, //s1
                    {0,   100, 0,   0,   100, 100, 0,   0,   0,  0,  0}, //s2
                    {0,   100, 0,   0,   100, 100, 100, 0,   0,  0,  0}, //s3
                    {0,   100, 100, 100, 0,   100, 100, 0,   0,  0,  0}, //s4
                    {0,   0,   100, 100, 100, 0,   100, 0,   0,  0,  0}, //s5
                    {0,   0,   0,   100, 100, 100, 0,   100, 100,100,100}, //s6
                    {0,   0,   0,   0,   0,   0,   100, 0,   0,  0,  0}, //c1
                    {0,   0,   0,   0,   0,   0,   100, 0,   0,  0,  0},  //c2
                    {0,   0,   0,   0,   0,   0,   100, 0,   0,  0,  0}, //c3
                    {0,   0,   0,   0,   0,   0,   100, 0,   0,  0,  0}  //c4
                };
        */

        /*
                for (auto &row_vertex : link_capacity)
                {
                    for (auto &col_vertex : row_vertex)
                    {
                        col_vertex *= toByte;
                    }
                }
        */
        /*
         cout << "link capacity\n";
         for (auto &cols : link_capacity)
         {
             for (auto &vertex : cols)
             {
                 cout << setw(9);
                 cout << vertex << " ";
             }
             cout << "\n";
         }
         */
    }

    void set_b_ij()
    {
        for (int i = 0; i < b_ij.size(); i++)
        {
            for (int j = 0; j < b_ij.size(); j++)
            {

                b_ij[i][j] = link_capacity[i][j];
                // cout << (b_ij[i][j] * 8) / (1000 * 1000) << "\t";
            }
            // cout << "\n";
        }
    } // end of set_bij()

    // sets client side sws connections
    void set_C_OF_SWs_Connections()
    {
        // cout << "-----------------------------------------set_C_OF_SWs_Connections\n";

        for (int i : ClientSideOFSWs)
        {
            for (int j : OF_SWs)
            {
                auto itr = ClientSideOFSWs.find(j); // if the neighboor is not server site sw
                if (e[i][j] == 1 && itr == ClientSideOFSWs.end())
                {
                    // cout << "set_C_OF_SWs_Connections: " << i << "--->" << j << ", ";

                    C_OF_SWs_Connections[i].emplace(j);
                }
            }
            // cout << "\n";
        }
    }

    // sets Server_OF_SWs_Connections
    void set_Server_OF_SWs_Connections()
    {
        // cout << "-----------------------------------------Server_OF_SWs_Connections: ";
        for (int i : ServerSideOFSWs)
        {

            for (int j : OF_SWs)
            {
                // auto itr = find( ServerSideOFSWs.begin(), ServerSideOFSWs.end(), j );
                auto itr = ServerSideOFSWs.find(j); // if the neighboor is not server site sw
                if (e[i][j] == 1 && itr == ServerSideOFSWs.end())
                {
                    // cout << i << "--->" << j << ", ";
                    Server_OF_SWs_Connections[i].emplace(j);
                }
            }
            // cout << "\n";
            for (int k : ClientSideOFSWs)
            {
                auto itr = ServerSideOFSWs.find(k); // if the neighboor is not server site sw
                if (e[i][k] == 1 && itr == ServerSideOFSWs.end())
                {
                    // cout << i << "--->" << k << ", ";
                    Server_OF_SWs_Connections[i].emplace(k);
                }
            }
        }
    }

    void set_ServerSideOFSWs_Connected_Servers()
    {
        for (int i : ServerSideOFSWs)
        {
            for (int j = 0; j < srv_qty; j++)
            {
                if (e[i][j] == 1)
                {
                    ServerSideOFSWs_Connected_Servers[i].emplace(j);
                }
            }
        }
    }

    /*
    void set_C_N_i_client_ofsw()
    {
        for (int i = srv_qty; i < srv_qty + sw_qty; i++)        //All OFSWs
        {                                                       //each OFSWs will be walked through
            for (int j = srv_qty + sw_qty; j < vertex_qty; j++) //All clients
            {                                                   //each client traversed
                if (e[i][j] == 1)
                { //if a ofsw is connected to a client that ofsw's index i is inserted to set C
                    C.emplace(i);
                    N_i[i].emplace(j);
                    client_ofsw[j].emplace(i);
                }
            }
        }
    }
*/
    void set_OF_SWs()
    {
        for (int i = srv_qty; i < srv_qty + sw_qty; i++)
        { // each OFSWs will be walked through
            for (int j : ClientSideOFSWs)
            { // client side OFSWs
                if (i == j)
                {
                    i_j_equal = true;
                    break;
                }
            }
            if (!i_j_equal)
            {
                OF_SWs.emplace_back(i);
            }
            i_j_equal = false;
        }

        // All OF SWs except client side OF SWs
        for (int i = 0; i < srv_qty + sw_qty; i++)
        {
            for (int j : OF_SWs)
            {
                if (e[i][j] == 1)
                {
                    OF_SWs_Connections[j].emplace(i);
                }
            }
        }
    } // end of set_OF_SWs()

    // All sws except client and server site sws.
    void set_OF_SWs_No_SSSWs()
    {
        // All OF SWs except client end server side OF SWs
        for (int i : OF_SWs)
        {
            auto itr = ServerSideOFSWs.find(i);
            if (itr == ServerSideOFSWs.end())
                OF_SWs_No_SSSWs.emplace_back(i);
        }
        // All OF SWs except client side and server side OF SWs
        for (int i : OF_SWs_No_SSSWs) // first switch index in e starts from srv_qty
        {
            for (int j : OF_SWs)
            {
                if (e[i][j] == 1)
                {
                    OF_SWs_No_SSSWs_Connections[i].emplace(j);
                }
            }

            for (int j : ClientSideOFSWs)
            {
                if (e[i][j] == 1)
                {
                    OF_SWs_No_SSSWs_Connections[i].emplace(j);
                }
            }
        }
        /*
        cout << "OF_SWs_No_SSSWs_Connections\n";
        for (auto i : OF_SWs_No_SSSWs_Connections)
        {
            for (int j : i.second)
            {
                cout << i.first << "-->" << j << "\n";
            }
        }
        */
    } // end of set_OF_SWs_No_SSSWs()

    /*
    //Client side OFSWs and their connected clients print
    for (auto &i : N_i)
    {
        cout << i.first << " (client side ofsw) "
             << ": ";
        for (auto &j : i.second)
        {
            cout << j << " --- ";
        }
        cout << " (connected clients) "
             << "\n";
    }

    for (auto c : C)
    {
        cout << "Client side sws index : " << c << "\n";
    }

    for (auto ofsw : OF_SWs)
    {
        cout << "of sws index : " << ofsw << "\n";
    }

    for (auto client : client_ofsw)
    {
        cout << "client ofsw connection : " << client.first << ": " << client.second << "\n";
    }

*/
}; // end of class Net_Topo

// obtains video file names and sizes from HTTP Media Server repository/directory and saves them in files_sizes map.
void get_video_file_sizes(unordered_map<string, double> &files_sizes)
{
    std::string path = "./mediaServer/BBB/I/segs/1080p";
    // std::string path = "./mediaServer/ED/I/segs/1080p";

    for (const auto &entry : fs::directory_iterator(path))
    {
        files_sizes.insert(std::make_pair(entry.path().filename(), fs::file_size(entry.path())));
    }
    /*
    for (auto &e : files_sizes){
    std::cout << e.first << " : "<< e.second << std::endl;
    }
    */
}

void set_b_bar_cl(vector<vector<double>> &b_bar_cl, int requests_qty, int m_c, unordered_map<string, double> &files_sizes, double teta, int seg_index)
{
    double toMegabit = 8.0 / (1000 * 1000);
    for (int i = 0; i < requests_qty; i++)
    {
        // each layer size (as byte) is recorded to b_bar_cl for client c's segment request

        string base_file_name = "BBB-I-1080p.seg" + std::to_string(seg_index) + "-L0.svc";
        base_file_name = base_file_name.substr(0, base_file_name.rfind(".svc") - 1);
        string file_name;
        const int http_header_size = 500; // Ortalama header size 500 Byte olarak belirlenmiştir. İHTİYAÇ DUYULURSA DAHA KESİN BİR DEĞER GİRİLEBİLİR.
        for (int j = 0; j < m_c; j++)
        {
            // layer of requested segment's file name is obtained
            // each layer size of segment is recorded and converted to bandwith requirement (Byte per second) diving file size to 2(teta)
            file_name = base_file_name + std::to_string(j) + ".svc";
            b_bar_cl[i].emplace_back((/*Network Overhead*/ (1.054 * (files_sizes[file_name] + http_header_size)) * toMegabit) / teta); // Required BW to download this layer on time - Byte per second.
            // cout << "file_name: " << file_name << " --- " << "Required BW (Mb): " << b_bar_cl[i][j] << "\n";
        }
    }
} // end of update_b_bar_c_l func

int total_layer_qty(vector2d &b_bar_cl, int requests_qty)
{
    int total_layer_qty = 0;
    for (int i = 0; i < requests_qty; i++)
    {
        total_layer_qty += b_bar_cl[i].size();
    }
    return total_layer_qty;
}
// multiserver(multiserverEnv, net_topo, b_bar_cl, m_c, r_sc_sol, r_sc_gamma_sol, gamma_ij_sol, provided_rate_for_c);

void multiserver(IloEnv multiserverEnv, Net_Topo &net_topo, vector<vector<double>> &b_bar_cl, int m_c, IloNumArray2 &r_sc_sol, IloNumArray2 &r_sc_gamma_sol,
                 IloNumArray2 &gamma_ij_sol, vector<double> &provided_rate_for_c, IloNumArray4 &f_sc_ij_sol)
{
    IloModel model(multiserverEnv);
    int server_site_sws_qty = net_topo.ServerSideOFSWs.size();
    int client_site_sws_qty = net_topo.ClientSideOFSWs.size();
    IloNumVarArray2 r_sc(multiserverEnv, server_site_sws_qty);       // variables to get bw rate server site sws to client site sws
    IloNumVarArray2 r_sc_gamma(multiserverEnv, server_site_sws_qty); // variables to get incrase bw rate server site sws to client site sws to get robustness against fractional reduction since layers bw need and bw fraction may not be same
    // std::map<int, double> req_max_rates_from_cssws;               // keeps required max data rate for clients site sws
    IloNumVarArray4 f_sc_ij(multiserverEnv, server_site_sws_qty); // variables to get bw rate on all edges for sssws (server site sws) and cssws (client site sws)
    IloNumVarArray2 gamma_ij(multiserverEnv, (net_topo.srv_qty + net_topo.sw_qty));

    /*
    // calculating cssw's required max data rates
    for (int c = 0; c < net_topo.requests_qty; c++)
    {
        req_max_rates_from_cssws[net_topo.sw_qty] = 0;
    }

    for (int c = 0; c < net_topo.requests_qty; c++)
    {
        int client_c_rate = 0;
        for (int l = 0; l < m_c; l++)
        {
            client_c_rate += b_bar_cl[c][l]; // requested files from c * file size / TETA
        }
        req_max_rates_from_cssws[net_topo.sw_qty] += client_c_rate;
    }
    */
    /*
    cout << "************************Required bit rate: ";
    for (auto cssw : req_max_rates_from_cssws)
    {
        cout << cssw.first << " --- " << cssw.second << "\n";
    }
    */
    // object function for r_sc
    IloExpr exp_r_sc_obj(multiserverEnv);
    // r_sc variables are declared & initilized
    for (int s = 0; s < server_site_sws_qty; s++)
    {
        r_sc[s] = IloNumVarArray(multiserverEnv, client_site_sws_qty);
        r_sc_gamma[s] = IloNumVarArray(multiserverEnv, client_site_sws_qty);
        for (int c = 0; c < client_site_sws_qty; c++)
        {
            r_sc[s][c] = IloNumVar(multiserverEnv, 0, IloInfinity);
            r_sc_gamma[s][c] = IloNumVar(multiserverEnv, 0, IloInfinity);
            exp_r_sc_obj += r_sc[s][c]; // + r_sc_gamma[s][c];
        }
    }
    /*
    // BW upper bound according to y
    //  constants for r_sc --- r_sc[0][0] + r_sc[1][0] <= max_rate_of_c[0]
    auto max_rate_of_c = req_max_rates_from_cssws.begin();
    for (int c = 0; c < client_site_sws_qty; c++)
    {
        IloExpr exp_r_sc_const(multiserverEnv);
        for (int s = 0; s < server_site_sws_qty; s++)
        {
            exp_r_sc_const += r_sc[s][c];
        }
        // cout << "*******************++++++++++++++++++++++++++*************max_rate_of_c: " << max_rate_of_c->first << ": " << max_rate_of_c->second << "\n";
        model.add(exp_r_sc_const <= max_rate_of_c->second);
        exp_r_sc_const.end();
        max_rate_of_c++;
    }
    */
    //The constraint, upper bound according to x's connections
    for (auto x : net_topo.Server_OF_SWs_Connections)
    {
        double bit_rate_of_x = 0.0;
        // long double bit_rate_of_x = 0.0;
        for (auto j : x.second)
        {
            bit_rate_of_x += net_topo.b_ij[x.first][j];
        }
        IloExpr exp_r_sc_const2(multiserverEnv);
        for (int y = 0; y < client_site_sws_qty; y++)
        {
            exp_r_sc_const2 += r_sc[x.first - net_topo.srv_qty][y];
        }
        model.add(exp_r_sc_const2 <= bit_rate_of_x);
        exp_r_sc_const2.end();
    }

    /*
    for (int s = 0; s < server_site_sws_qty; s++)
    {
        for (int c = 0; c < client_site_sws_qty; c++)
        {
            model.add(r_sc_gamma[s][c] <= r_sc[s][c]/20.0);
        }
    }
    */
    for (int i = 0; i < net_topo.sw_qty + net_topo.srv_qty; i++)
    {
        gamma_ij[i] = IloNumVarArray(multiserverEnv, (net_topo.sw_qty + net_topo.srv_qty));
        for (int j = 0; j < (net_topo.sw_qty + net_topo.srv_qty); j++)
        {
            gamma_ij[i][j] = IloNumVar(multiserverEnv, 0, net_topo.link_capacity[i][j] / (double)10.0); // If there is enough capacity, it tries to keep 10% of the link capacity free against burst usage
            if (net_topo.e[i][j] == 0)
            {
                gamma_ij[i][j].setBounds(0, 0);
            }
        }
    }

    // No need for servers
    for (int i = 0; i < net_topo.srv_qty; i++)
    {
        for (int j = 0; j < (net_topo.sw_qty + net_topo.srv_qty); j++)
        {
            gamma_ij[i][j].setBounds(0, 0);
        }
    }

    // f_sc_ij and f_sc_gamma_ij variables are declared & initilized
    for (int s = 0; s < server_site_sws_qty; s++)
    {
        f_sc_ij[s] = IloNumVarArray3(multiserverEnv, client_site_sws_qty);
        // f_sc_gamma_ij[s] = IloNumVarArray3(multiserverEnv, client_site_sws_qty);
        for (int c = 0; c < client_site_sws_qty; c++)
        {
            f_sc_ij[s][c] = IloNumVarArray2(multiserverEnv, (net_topo.sw_qty + net_topo.srv_qty));
            // f_sc_gamma_ij[s][c] = IloNumVarArray2(multiserverEnv, (net_topo.sw_qty + net_topo.srv_qty));
            for (int i = 0; i < net_topo.sw_qty + net_topo.srv_qty; i++)
            {
                f_sc_ij[s][c][i] = IloNumVarArray(multiserverEnv, (net_topo.sw_qty + net_topo.srv_qty), 0, IloInfinity); // defining vertex array's 2nd dimention which contain cplex variable array
                // f_sc_gamma_ij[s][c][i] = IloNumVarArray(multiserverEnv, (net_topo.sw_qty + net_topo.srv_qty), 0, IloInfinity); // defining vertex array's 2nd dimention which contain cplex variable array
                for (int j = 0; j < (net_topo.sw_qty + net_topo.srv_qty); j++)
                {
                    // cout << "init scij: " << s << c << i << j << "\n";
                    if (net_topo.e[i][j] == 0)
                    {
                        f_sc_ij[s][c][i][j].setBounds(0, 0);
                    }
                    else
                    {
                        // f_sc_gamma_ij[s][c][i][j] = IloNumVar(multiserverEnv, 0, net_topo.link_capacity[i][j] / 10.0);
                    }
                }
            }
            // No need servers
            for (int i = 0; i < net_topo.srv_qty; i++)
            {
                for (int j = 0; j < (net_topo.sw_qty + net_topo.srv_qty); j++)
                {
                    f_sc_ij[s][c][i][j].setBounds(0, 0);
                    // f_sc_gamma_ij[s][c][i][j].setBounds(0, 0);
                }
            }
        }
    }

    // IloArray<IloArray<IloExprArray>> f_sc_ij_const_1_ExprArr(multiserverEnv, server_site_sws_qty);
    //  f_sc_ij constraint 1 --- if i Element of SSSWs

    for (int s = 0; s < server_site_sws_qty; s++)
    {
        // f_sc_ij_const_1_ExprArr[s] =  IloArray<IloExprArray>(multiserverEnv, client_site_sws_qty);
        for (int c = 0; c < client_site_sws_qty; c++)
        {
            IloExprArray f_sc_ij_const_1_ExprArr(multiserverEnv, 2);
            f_sc_ij_const_1_ExprArr[0] = IloExpr(multiserverEnv);
            f_sc_ij_const_1_ExprArr[1] = IloExpr(multiserverEnv);

            auto i = net_topo.Server_OF_SWs_Connections.find(s + net_topo.srv_qty);

            // auto i = net_topo.Server_OF_SWs_Connections[s + net_topo.srv_qty];//sws e index comes after servers
            if (i != net_topo.Server_OF_SWs_Connections.end())
            {
                for (auto j : i->second)
                {
                    // cout << "f_sc_ij: ";
                    // cout << s << c << i->first << j << "\n";
                    f_sc_ij_const_1_ExprArr[0] += f_sc_ij[s][c][i->first][j]; // - f_sc_gamma_ij[s][c][i->first][j];
                    f_sc_ij_const_1_ExprArr[1] += f_sc_ij[s][c][j][i->first]; // - f_sc_gamma_ij[s][c][j][i->first];
                    // cout << "f_" << s << c << i->first << j << " - " << "f_" << s << c << j << i->first << "\n";
                }
            }

            // cout << " = r_" << s << c << "\n";
            model.add(f_sc_ij_const_1_ExprArr[0] - f_sc_ij_const_1_ExprArr[1] == r_sc[s][c]); // + r_sc_gamma[s][c]);
            // cout << "f_sc_ij_const_1_ExprArr[0]: " << f_sc_ij_const_1_ExprArr[0] << "\n";

            f_sc_ij_const_1_ExprArr.end();
        }
    }

    // f_sc_ij constraint 2 --- if i = V \ S U C
    for (int s = 0; s < server_site_sws_qty; s++)
    {
        // f_sc_ij_const_1_ExprArr[s] =  IloArray<IloExprArray>(multiserverEnv, client_site_sws_qty);
        for (int c = 0; c < client_site_sws_qty; c++)
        {

            // cout << "cons 2 \n";
            int count_i = 2;
            for (auto i : net_topo.OF_SWs_No_SSSWs)
            {
                IloExprArray f_sc_ij_const_2_ExprArr(multiserverEnv, 2);
                f_sc_ij_const_2_ExprArr[0] = IloExpr(multiserverEnv);
                f_sc_ij_const_2_ExprArr[1] = IloExpr(multiserverEnv);
                // cout << "f_sc_ij: " << s << c << i << "_";

                for (auto j : net_topo.OF_SWs_No_SSSWs_Connections[i])
                {
                    // cout << j << " - ";

                    // cout << "cons 2 --- f_sc_ij: "<< s << c << i << j <<" - " << "cons 2 --- f_sc_ji: "<< s << c << j << i <<"\n";
                    // cout << s << c << i << j << "\n";
                    f_sc_ij_const_2_ExprArr[0] += f_sc_ij[s][c][i][j]; // - f_sc_gamma_ij[s][c][i][j];
                    f_sc_ij_const_2_ExprArr[1] += f_sc_ij[s][c][j][i]; // - f_sc_gamma_ij[s][c][j][i];
                }
                model.add(f_sc_ij_const_2_ExprArr[0] - f_sc_ij_const_2_ExprArr[1] == 0);
                // cout <<    "count_i: " << count_i++<<"\n";
                // cout <<"f_sc_ij_const_2_ExprArr[1]: "<<f_sc_ij_const_2_ExprArr[1]<<"\n";
                f_sc_ij_const_2_ExprArr.end();
            }
        }
    }

    /*
    // f_sc_ij constraint 3 --- if i  Elenment of Clients site switches
    for (int s = 0; s < server_site_sws_qty; s++)
    {
        // f_sc_ij_const_1_ExprArr[s] =  IloArray<IloExprArray>(multiserverEnv, client_site_sws_qty);
        for (int c = 0; c < client_site_sws_qty; c++)
        {
            IloExprArray f_sc_ij_const_3_ExprArr(multiserverEnv, 2);
            f_sc_ij_const_3_ExprArr[0] = IloExpr(multiserverEnv);
            f_sc_ij_const_3_ExprArr[1] = IloExpr(multiserverEnv);
            auto i = net_topo.C_OF_SWs_Connections.find(c + net_topo.srv_qty + net_topo.OF_SWs.size());
            cout << "cons 3 \n";
            if (i != net_topo.C_OF_SWs_Connections.end())
            {
                cout << "f_sc_ij: "<< s << c << i->first <<"_";
                for (auto j : i->second)
                {

                    cout << j << " - ";
                    // cout << s << c << i << j << "\n";
                    //  cout <<"scij: " << s << c << i<< j <<"\n";

                    f_sc_ij_const_3_ExprArr[0] += f_sc_ij[s][c][i->first][j]; // - f_sc_gamma_ij[s][c][i->first][j];
                    f_sc_ij_const_3_ExprArr[1] += f_sc_ij[s][c][j][i->first]; // - f_sc_gamma_ij[s][c][j][i->first];
                }

                model.add(f_sc_ij_const_3_ExprArr[0] - f_sc_ij_const_3_ExprArr[1] == -r_sc[s][c]);
            }
            f_sc_ij_const_3_ExprArr.end();
        }
    }
    */

    // BW limitation
    for (int i = net_topo.srv_qty; i < net_topo.sw_qty + net_topo.srv_qty; i++)
    {
        for (int j = net_topo.srv_qty; j < net_topo.sw_qty + net_topo.srv_qty; j++)
        {
            IloExpr f_sc_ij_bw_expr(multiserverEnv);
            for (int s = 0; s < server_site_sws_qty; s++)
            {
                for (int c = 0; c < client_site_sws_qty; c++)
                {
                    if (net_topo.e[i][j] == 1)
                    {
                        f_sc_ij_bw_expr += f_sc_ij[s][c][i][j]; //+ f_sc_gamma_ij[s][c][i][j];
                        // cout << "cons 4 --- f_sc_ij: ";
                        // cout << s << c << i << j << "\n";
                    }
                }
            }
            if (net_topo.e[i][j] == 1)
            {
                model.add(f_sc_ij_bw_expr + gamma_ij[i][j] <= net_topo.b_ij[i][j]);
                // cout << "net_topo.b_ij["<< i<< "]["<<j<<"]" <<net_topo.b_ij[i][j] << "\t";
            }
            f_sc_ij_bw_expr.end();
        }
        // cout << "\n";
    }

    // minimize BW usage by selecting shortest path
    IloExpr f_sc_ij_obj_expr(multiserverEnv);
    // IloExpr f_sc_gamma_ij_obj_expr(multiserverEnv);
    for (int s = 0; s < server_site_sws_qty; s++)
    {
        for (int c = 0; c < client_site_sws_qty; c++)
        {
            for (int i = net_topo.srv_qty; i < net_topo.sw_qty + net_topo.srv_qty; i++)
            {
                for (int j = net_topo.srv_qty; j < net_topo.sw_qty + net_topo.srv_qty; j++)
                {
                    if (net_topo.e[i][j] == 1)
                    {
                        // cout << "const minimize BW --- f_sc_ij: ";
                        // cout << s << c << i << j << "\n";
                        f_sc_ij_obj_expr += f_sc_ij[s][c][i][j];
                        // f_sc_gamma_ij_obj_expr += f_sc_gamma_ij[s][c][i][j];
                    }
                }
            }
        }
    }

    IloExpr gamma_ij_obj_expr(multiserverEnv);
    for (int i = net_topo.srv_qty; i < net_topo.sw_qty + net_topo.srv_qty; i++)
    {
        for (int j = net_topo.srv_qty; j < net_topo.sw_qty + net_topo.srv_qty; j++)
        {
            if (net_topo.e[i][j] == 1)
            {
                // gamma_ij_obj_expr += gamma_ij[i][j]*( 1.2 - net_topo.b_ij[i][j]/net_topo.link_capacity[i][j] );
                // gamma_ij_obj_expr += gamma_ij[i][j] * (1.5 - net_topo.b_ij[i][j] / net_topo.link_capacity[i][j]);
                // gamma_ij_obj_expr += gamma_ij[i][j] * (1.0 + net_topo.b_ij[i][j] / net_topo.link_capacity[i][j]);
                // gamma_ij_obj_expr += gamma_ij[i][j] * (2.0 - net_topo.b_ij[i][j] / net_topo.link_capacity[i][j]);
                gamma_ij_obj_expr += gamma_ij[i][j];
            }
        }
    }
    // model.add(IloMinimize(multiserverEnv, -10 * exp_r_sc_obj + f_sc_ij_obj_expr - gamma_ij_obj_expr /*- f_sc_gamma_ij_obj_expr*/));

    // model.add(IloMinimize(multiserverEnv, -10 * exp_r_sc_obj + f_sc_ij_obj_expr));
    model.add(IloMinimize(multiserverEnv, -10 * exp_r_sc_obj + f_sc_ij_obj_expr - gamma_ij_obj_expr));

    // model.add(IloMinimize(multiserverEnv, -30 * exp_r_sc_obj + f_sc_ij_obj_expr - 2*gamma_ij_obj_expr /*- f_sc_gamma_ij_obj_expr*/));
    // model.add(IloMinimize(multiserverEnv, -100 * exp_r_sc_obj + 3*f_sc_ij_obj_expr - 2*gamma_ij_obj_expr /*- f_sc_gamma_ij_obj_expr*/));
    // model.add(IloMinimize(multiserverEnv, -10 * exp_r_sc_obj + f_sc_ij_obj_expr));
    // model.add(IloMinimize(multiserverEnv, -100000 * exp_r_sc_obj + f_sc_ij_obj_expr));
    f_sc_ij_obj_expr.end();
    // model.add(IloMinimize(multiserverEnv, -100 * exp_r_sc_obj));
    exp_r_sc_obj.end();
    gamma_ij_obj_expr.end();
    IloCplex multiserverCplex(model);
    multiserverCplex.setOut(multiserverEnv.getNullStream()); // Disable CPLEX logging
    multiserverCplex.setWarning(multiserverEnv.getNullStream());
    // multiserverCplex.setParam(IloCplex::Param::TimeLimit, 1.0);

    if (multiserverCplex.solve())
    {
        // IloNumArray4 f_sc_ij_sol(multiserverEnv, server_site_sws_qty);

        multiserverCplex.exportModel("multiServerModel.lp");
        IloAlgorithm::Status solStatus = multiserverCplex.getStatus();
        // cout << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!multiserver Status: " << solStatus << "\n";
        //  multiserverCplex.getValues(f_sc_ij[s][c][i]);

        for (int s = 0; s < server_site_sws_qty; s++)
        {
            // r_sc_sol[s] = IloNumArray(multiserverEnv, client_site_sws_qty);
            multiserverCplex.getValues(r_sc[s], r_sc_sol[s]);
            // multiserverCplex.getValues(r_sc_gamma[s], r_sc_gamma_sol[s]);
            // cout << "r_sc[" << s << "]" << r_sc_sol[s] << "\n";
        }

        for (int i = net_topo.srv_qty; i < (net_topo.srv_qty + net_topo.sw_qty); i++)
        {
            for (int j = net_topo.srv_qty; j < net_topo.srv_qty + net_topo.sw_qty; j++)
            {
                if (net_topo.e[i][j] == 1)
                {
                    gamma_ij_sol[i][j] = multiserverCplex.getValue(gamma_ij[i][j]);

                    // if (gamma_ij_sol[i][j] > 0)
                    // cout << "gamma_ij_sol[" << i << j << "]" << gamma_ij_sol[i][j] << "\n";
                    // cout <<"net_topo.link_capacity["<<i<<"]["<<j<<"]:" << net_topo.link_capacity[i][j]<<"\n";
                }
            }
        }

        // IloArray<IloArray<IloArray<IloNumArray>>> f_sc_ij_sol(multiserverEnv, server_site_sws_qty);
        // IloNumArray4 f_sc_gamma_ij_sol(multiserverEnv, server_site_sws_qty);
        for (int s = 0; s < server_site_sws_qty; s++)
        {
            f_sc_ij_sol[s] = IloNumArray3(multiserverEnv, client_site_sws_qty);
            for (int c = 0; c < client_site_sws_qty; c++)
            {
                f_sc_ij_sol[s][c] = IloNumArray2(multiserverEnv, (net_topo.sw_qty + net_topo.srv_qty));
                for (int i = net_topo.srv_qty; i < (net_topo.srv_qty + net_topo.sw_qty); i++)
                {
                    f_sc_ij_sol[s][c][i] = IloNumArray(multiserverEnv, (net_topo.sw_qty + net_topo.srv_qty));
                    for (int j = net_topo.srv_qty; j < net_topo.srv_qty + net_topo.sw_qty; j++)
                    {
                        if (net_topo.e[i][j] == 1)
                        {
                            f_sc_ij_sol[s][c][i][j] = multiserverCplex.getValue(f_sc_ij[s][c][i][j]);
                            // IloNum f_sc_ij_sol = multiserverCplex.getValue(f_sc_ij[s][c][i][j]);
                            //  IloNum f_sc_gamma_ij_sol = multiserverCplex.getValue(f_sc_gamma_ij[s][c][i][j]);
                            //  if (f_sc_ij_sol > 0)
                            //  cout << "f_sc_ij_sol[" << s << c << i << j << "]" << f_sc_ij_sol << "\n";
                        }
                    }
                }
            }
        }
        /*
        for (int i = net_topo.srv_qty; i < (net_topo.srv_qty + net_topo.sw_qty); i++)
        {
            for (int j = net_topo.srv_qty; j < net_topo.srv_qty + net_topo.sw_qty; j++)
            {
                if (net_topo.e[i][j] == 1)
                {
                    IloNum gamma_ij_sol = multiserverCplex.getValue(gamma_ij[i][j]);
                    if (gamma_ij_sol > 0)
                        cout << "gamma_ij_sol[" << i << j << "]" << gamma_ij_sol << "\n";
                    cout <<"net_topo.link_capacity["<<i<<"]["<<j<<"]:" << net_topo.link_capacity[i][j]<<"\n";
                }
            }
        }
        */
    }

    // updates provided_rate_for_c vector
    int cssw_qty = net_topo.ClientSideOFSWs.size();
    int sssw_qty = net_topo.ServerSideOFSWs.size();

    for (int s = 0; s < sssw_qty; s++)
    {
        for (int c = 0; c < cssw_qty; c++)
        {
            provided_rate_for_c[c] = 0;
        }
    }

    for (int s = 0; s < sssw_qty; s++)
    {
        for (int c = 0; c < cssw_qty; c++)
        {
            provided_rate_for_c[c] += r_sc_sol[s][c];
        }
    }
    for (int c = 0; c < cssw_qty; c++)
    {
        if (provided_rate_for_c[c] == 0)
        {
            cout << "---!!! Multiserver() called again\n";
            multiserver(multiserverEnv, net_topo, b_bar_cl, m_c, r_sc_sol, r_sc_gamma_sol, gamma_ij_sol, provided_rate_for_c, f_sc_ij_sol);
        }
    }

} // End of multiserver function

//This function is used to initilize IBM CPLEX variables during at our first approach (before OPM) which we find the solution up 8 iteration. Later we keep it even no more than 1 iteration between master (OPM) and worker (CPM). 
void masterInitBuilder(IloEnv masterEnv, IloIntVarArray3 w_s_cl, IloNumVar Q, IloNumVar L, IloNumVarArray T_c, IloNumVarArray I_c, IloIntVarArray v_c, IloNumVarArray N_c, int requests_qty,
                       vector2d b_bar_cl, Net_Topo net_topo, int m_c, int a_s_cl, IloExpr masterOptConstExpr, IloArray<IloRangeArray> masterConst1_RangeArr,
                       IloArray<IloArray<IloRangeArray>> masterConst2_RangeArr, IloArray<IloRangeArray> masterConst3_RangeArr, IloRangeArray masterConst4_RangeArr1, IloRangeArray masterConst4_RangeArr2,
                       IloRangeArray masterConst5_RangeArr, IloRangeArray masterConst6_RangeArr, IloRangeArray masterConst7_RangeArr1,
                       IloRangeArray masterConst7_RangeArr2, IloRangeArray masterConst8_RangeArr, int phi_c)
{
    int layer_qty;

    // w_s_cl vars init & masterOptConstExpr init
    for (int i = 0; i < requests_qty; i++)
    {
        layer_qty = m_c;
        // cout << "m_c 11:" << m_c << "\n";

        // layer_qty = b_bar_cl[i].size();
        w_s_cl[i] = IloArray<IloIntVarArray>(masterEnv, layer_qty); // defining layer array of each c
        for (int j = 0; j < layer_qty; j++)
        {
            w_s_cl[i][j] = IloIntVarArray(masterEnv, net_topo.srv_qty, 0, 1); // defining cplex variable array for w
            for (int k = 0; k < net_topo.srv_qty; k++)
            {
                // w_s_cl[i][j][k] = IloNumVar(env, 0, 1, IloNumVar::Int); //cplex variables definition which is about if server will be send the layer or not to c
                char varName[100]; // used to assign variable names in IntVarArrays
                sprintf(varName, "w_s:%d_c:%d_l:%d", k, i, j);
                w_s_cl[i][j][k].setName(varName);      // std::cout << typeid(t_cl_ij[i][j][k][l]).name() << " - "; //sub problem 1 objective funciton content
                                                       // std::cout << typeid(w_s_cl[i][j][k]).name() << " - "; //sub problem 1 objective funciton content
                                                       // each server - e array's first srv_qty element contraints servers. so loop starts from 0
                masterOptConstExpr += w_s_cl[i][j][k]; // builds masterOptConstExpr - TOPLAM KAÇ TANE W_S_CL == 1 OLDUĞUNU BELİRLEMEK İÇİN KULLANILIYOR.
            }
            // std::cout << "\n";
        }
        // std::cout << "\n";
    }

    /*
    // master problem server load balancing contraint
    for (int k = 0; k < net_topo.srv_qty; k++)
    { // each server - e array's first srv_qty element contraints servers. so loop starts from 0
        IloExpr master_srv_lb_expr(masterEnv);
        for (int i = 0; i < requests_qty; i++)
        { // each client (request)
            layer_qty = m_c;
            // layer_qty = b_bar_cl[i].size();
            for (int j = 0; j < layer_qty; j++)
            { // each layer of request
                // master_srv_lb_expr_arr[k] += w_s_cl[i][j][k];
                master_srv_lb_expr += w_s_cl[i][j][k];
            }
        }
        master_srv_lb_expr_arr[k] = master_srv_lb_expr;
    } // End of master problem server load balancing contraint
    */

    // master problem constraint (1)
    for (int i = 0; i < requests_qty; i++)
    { // each client (request)
        // layer_qty = b_bar_cl[i].size();
        layer_qty = m_c;
        masterConst1_RangeArr[i] = IloRangeArray(masterEnv, layer_qty);
        for (int j = 0; j < layer_qty; j++)
        { // each layer of request
            IloExpr master_const1_expr(masterEnv);
            for (int k = 0; k < net_topo.srv_qty; k++)
            { // each server - e array's first srv_qty element contraints servers. so loop starts from 0
                master_const1_expr += w_s_cl[i][j][k];
            }
            masterConst1_RangeArr[i][j] = (master_const1_expr <= 1); // CONST 1
            master_const1_expr.end();
        }
    } // end of master problem constrain (1)

    // master problem constraint (2)
    for (int i = 0; i < requests_qty; i++)
    { // each client (request)
        // layer_qty = b_bar_cl[i].size();
        layer_qty = m_c;
        // cout << "m_c 14:" << m_c << "\n";

        masterConst2_RangeArr[i] = IloArray<IloRangeArray>(masterEnv, layer_qty);
        for (int j = 0; j < layer_qty; j++)
        { // each layer of request
            masterConst2_RangeArr[i][j] = IloRangeArray(masterEnv, net_topo.srv_qty);
            for (int k = 0; k < net_topo.srv_qty; k++)
            {                                                                 // each server - e array's first srv_qty element contains servers. so loop starts from 0
                masterConst2_RangeArr[i][j][k] = (w_s_cl[i][j][k] <= a_s_cl); // not actually used. We assumed that each server holds each video file.
            }
        }
    } // end of master problem constrain (2)

    // master problem constraint (3)
    for (int i = 0; i < requests_qty; i++)
    { // each client (request)
        // layer_qty = b_bar_cl[i].size();
        layer_qty = m_c;
        // cout << "m_c 15:" << m_c << "\n";

        masterConst3_RangeArr[i] = IloRangeArray(masterEnv, layer_qty - 1);
        for (int j = 0; j < layer_qty - 1; j++)
        { // each layer of request
            IloExpr master_const3_expr1(masterEnv);
            IloExpr master_const3_expr2(masterEnv);
            for (int k = 0; k < net_topo.srv_qty; k++)
            { // each server - e array's first srv_qty element contains servers. so loop starts from 0
                master_const3_expr1 += w_s_cl[i][j + 1][k];
                master_const3_expr2 += w_s_cl[i][j][k];
            }
            masterConst3_RangeArr[i][j] = (master_const3_expr1 - master_const3_expr2 <= 0);
            master_const3_expr1.end();
            master_const3_expr2.end();
        }
    } // end of master problem constrain (3)

    // master problem constraint (4)
    for (int i = 0; i < requests_qty; i++)
    { // each client (request)
        layer_qty = m_c;
        // cout << "m_c 16:" << m_c << "\n";

        // layer_qty = b_bar_cl[i].size();
        IloExpr master_const4_expr1(masterEnv);
        for (int j = 0; j < layer_qty; j++)
        { // each layer of request
            for (int k = 0; k < net_topo.srv_qty; k++)
            { // each server - e array's first srv_qty element contains servers. so loop starts from 0
                master_const4_expr1 += w_s_cl[i][j][k];
            }
        }
        // cout << "************************************************m_c: " << m_c << "\n";
        masterConst4_RangeArr1[i] = (Q + master_const4_expr1 / (double)m_c >= 1.0);
        // masterConst4_RangeArr2[i] = (1.25 >= Q + master_const4_expr1 / (double)m_c);
        master_const4_expr1.end();
    } // master problem constraint (4)

    /*
    // master problem constraint (5)
    // T_max calculation
    int T_max = 0;
    for (int i = 0; i < requests_qty; i++)
    {
        int lambda_bar_i = lambda_bar_c[requests[i]->get_endpoint().address().to_string()];
        if (T_max < (lambda_bar_i + m_c))
        {
            T_max = lambda_bar_i + m_c;
        }
    }
    // T_max += m_c;
    //  cout << "T_max: " << T_max << "\n";
    */
    // master problem constraint (5)
    double T_max = 0.0;
    for (int i = 0; i < requests_qty; i++)
    {
        int lambda_bar_i = lambda_bar_c[i];
        int phi_i = phi_c;
        double T_i = (lambda_bar_i + m_c) / double(phi_i);
        if (T_max < T_i)
        {
            T_max = T_i;
        }
    }

    for (int i = 0; i < requests_qty; i++)
    {
        layer_qty = m_c;
        IloExpr master_const5_expr(masterEnv);
        for (int j = 0; j < layer_qty; j++)
        {
            for (int k = 0; k < net_topo.srv_qty; k++)
            { // each server - e array's first srv_qty element contraints servers. so loop starts from 0
                master_const5_expr += w_s_cl[i][j][k];
            }
        }
        IloInt lambda_bar_i = lambda_bar_c[i];
        IloInt phi_i = phi_c;
        masterConst5_RangeArr[i] = (T_c[i] - (lambda_bar_i + master_const5_expr) / (phi_i * T_max) == 0); // Need to maximize T_c
        // masterConst5_RangeArr[i] = (T_c[i] - (lambda_bar_i + master_const5_expr) / (double)T_max == 0); // Need to maximize T_c
        master_const5_expr.end();
    }
    // end of master problem constraint (5)

    // master problem constraint (6)
    // I_max calculation
    int I_max = 0;
    for (int i = 0; i < requests_qty; i++)
    {
        int mu_bar_i = mu_bar_c[i];
        if (I_max < (mu_bar_i + m_c))
        {
            I_max = mu_bar_i + m_c;
        }
    }
    // I_max += m_c;
    //  cout << "I_max: " << I_max << "\n";

    for (int i = 0; i < requests_qty; i++)
    {
        layer_qty = m_c;
        // cout << "m_c 18:" << m_c << "\n";

        // layer_qty = b_bar_cl[i].size();
        IloExpr master_const6_expr(masterEnv);
        for (int j = 0; j < layer_qty; j++)
        {
            for (int k = 0; k < net_topo.srv_qty; k++)
            { // each server - e array's first srv_qty element contraints servers. so loop starts from 0
                master_const6_expr += w_s_cl[i][j][k];
            }
        }
        int l_bar_i = l_bar_c[i];
        // cout << "l_bar_i: " << l_bar_i << "\n";
        int mu_bar_i = mu_bar_c[i];
        int phi_i = phi_c;
        // cout << "mu_bar_i: " << mu_bar_i << "\n";
        // cout << "master_const6_expr: " << master_const6_expr << "\n";

        masterConst6_RangeArr[i] = (I_c[i] - (mu_bar_i + IloAbs(master_const6_expr - l_bar_i)) / (double)I_max >= 0);
        masterConst7_RangeArr1[i] = (0 <= v_c[i] * m_c - IloAbs(master_const6_expr - l_bar_i));

        // masterConst5_RangeArr[i] =  ( T_c[i] + (lambda_bar_i + master_const5_expr)/(double)T_max == 1 );
        // masterConst5_RangeArr[i] =  ( (lambda_bar_i + master_const5_expr) / (T_c[i] * phi_i)  == T_max );
        // cout << "masterConst6_RangeArr[" << i << "]: " << masterConst6_RangeArr[i] << "\n";
        // cout << "masterConst7_RangeArr1[" << i << "]: " << masterConst7_RangeArr1[i] << "\n";
        master_const6_expr.end();
    }
    // end of master problem constraint (6)

    // master problem constraint (7)
    int N_max = 0;
    for (int i = 0; i < requests_qty; i++)
    {
        int v_bar_i = v_bar_c[i];
        if (N_max < v_bar_i)
        {
            N_max = v_bar_i;
        }
    }
    N_max += 1;
    // cout << "N_max: " << N_max << "\n";
    for (int i = 0; i < requests_qty; i++)
    {
        int v_bar_i = v_bar_c[i];
        // cout << "v_bar_i" << v_bar_i << "\n";
        int phi_i = phi_c; 

        masterConst7_RangeArr2[i] = (0 <= N_c[i] * N_max - (v_bar_i + v_c[i]));
        // cout << "masterConst7_RangeArr2[" << i << "]: " << masterConst7_RangeArr2[i] << "\n";
    }

    // master problem constraint (8)
    for (int i = 0; i < requests_qty; i++)
    { // each client (request)
        layer_qty = m_c;
        int lambda_bar_i = lambda_bar_c[i];
        int phi_i = phi_c;
        IloExpr master_const8_expr(masterEnv);
        for (int j = 0; j < layer_qty; j++)
        { // each layer of request
            for (int k = 0; k < net_topo.srv_qty; k++)
            { // each server - e array's first srv_qty element contains servers. so loop starts from 0
                master_const8_expr += w_s_cl[i][j][k];
            }
        }
        masterConst8_RangeArr[i] = (L + ((master_const8_expr + lambda_bar_i) / (phi_i * T_max)) >= 1.0);
        master_const8_expr.end();
    } // master problem constraint (4)

    /*
            if(segment_index == 0){

            }else{

            }
      */
    // if (l_bar_c[client_ip] == 0) l_bar_c[client_ip] = w_s_c_l_sol_for_i;//first segment exception
    // end of master problem constraint (6)

} // End of masterInitBuilder

//This function is explained as OPM in paper
void master(IloEnv masterEnv, IloIntVarArray3 w_s_cl, IloNumArray3 w_s_cl_sol, IloNumVar Q, IloNumVar L, IloNumVarArray T_c, IloNumVarArray I_c, IloIntVarArray v_c, IloIntArray v_c_sol, IloNumVarArray N_c,
            int requests_qty, vector2d &b_bar_cl, Net_Topo &net_topo, int m_c, int a_s_cl,
            IloExpr masterOptConstExpr, IloArray<IloRangeArray> masterConst1_RangeArr, IloArray<IloArray<IloRangeArray>> masterConst2_RangeArr, IloArray<IloRangeArray> masterConst3_RangeArr,
            IloRangeArray masterConst4_RangeArr1, IloRangeArray masterConst4_RangeArr2, IloRangeArray masterConst5_RangeArr, IloRangeArray masterConst6_RangeArr, IloRangeArray masterConst7_RangeArr1,
            IloRangeArray masterConst7_RangeArr2, IloRangeArray masterConst8_RangeArr,
            int &total_w_s_cl_ub, int &total_w_s_cl_max, IloNumArray2 &r_sc_sol, IloNumArray2 &r_sc_gamma_sol, const int &counter,
            std::map<int, double> &req_max_rates_from_cssws, IloNumArray2 &gamma_ij_sol, vector<vector<int>> &r_sc_w_s_cl_count, IloRangeArray &master_FeasCutArray, std::map<int, std::vector<int>> &sorted_r_sc_sol, std::set<int> &sending_sssws, std::vector<std::vector<int>> &combinations, int &nCr_counter, int &r_value, int &addition_to_sub_layer,
            bool &need_inc_add_sub_layer, int &inc_cancelled, vector<double> &provided_rate_for_c, bool &dec_buff_for_master, int &total_w_s_cl_result, bool &master_solved,
            int &last_infeas_total_w_s_cl, int &total_w_s_cl_sol, const int segment_index)
{
    try
    {
        IloModel masterMod(masterEnv, "masterMod");
        int layer_qty;
        int srv_qty = net_topo.srv_qty;
        int sw_qty = net_topo.sw_qty;
        int cssw_qty = net_topo.ClientSideOFSWs.size();
        int sssw_qty = net_topo.ServerSideOFSWs.size();

        IloExpr T_cs(masterEnv);
        IloExpr I_cs(masterEnv);
        IloExpr N_cs(masterEnv);
        for (int i = 0; i < requests_qty; i++)
        {
            T_cs += -T_c[i]; // to maximize T_c, multibled by negative coefficent
            I_cs += I_c[i];
            N_cs += N_c[i];
        }

        std::vector<int> senders_qty(cssw_qty);
        //cout << "counter:----------------------------> " << counter << "\n";
        if (counter == 0 || inc_cancelled > 0 || dec_buff_for_master)
        {
            // inc_cancelled = false;
            dec_buff_for_master = false;
            /*
            vector<double> provided_rate_for_c(cssw_qty);
            for (int s = 0; s < sssw_qty; s++)
            {
                for (int c = 0; c < cssw_qty; c++)
                {
                    provided_rate_for_c[c] += r_sc_sol[s][c];
                }
            }
            */
            // calculating cssw's required max data rates
            int requested_rate = 0;
            for (int c = 0; c < net_topo.requests_qty; c++)
            {
                for (int l = 0; l < m_c; l++)
                {
                    requested_rate += b_bar_cl[c][l]; // requested files from c * file size / TETA
                }
            }

            for (int c = 0; c < cssw_qty; c++)
            {
                int e_index_of_c = c + net_topo.srv_qty + net_topo.OF_SWs.size();
                auto req_max_rate_itr = req_max_rates_from_cssws.find(e_index_of_c);
                // cout << "Provided: " << provided_rate_for_c[c] * 8 / (1000 * 1000) << " < cssw:" << req_max_rate_itr->first << " -- " << req_max_rate_itr->second * 8 / (1000 * 1000) << "\n";
                cout << "Provided: " << provided_rate_for_c[c] << " < requested_rate:" << requested_rate << "\n";
                if (provided_rate_for_c[c] < req_max_rate_itr->second)
                {
                    cout << "******************BUFFER DECREASE NEED**************************\n";
                }
            }

            //---------------Sorting r_sc_sol according to bit rate ascending order
            std::vector<std::vector<int>> r_sc_sol_sort_indicator(sssw_qty, std::vector<int>(cssw_qty, 0));
            for (int cssw = 0; cssw < cssw_qty; cssw++) // her bir cssw için göndersici sssw'lerin r_sc'leri indexlerine göre sort ediliyor.
            {
                for (int sssw1 = 0; sssw1 < sssw_qty; sssw1++)
                {
                    int min_r_sc_sol = std::numeric_limits<int>::max();
                    int s;
                    for (int sssw2 = 0; sssw2 < sssw_qty; sssw2++)
                    {
                        if (r_sc_sol_sort_indicator[sssw2][cssw] == 0)
                        {
                            if (min_r_sc_sol > r_sc_sol[sssw2][cssw])
                            {
                                min_r_sc_sol = r_sc_sol[sssw2][cssw];
                                s = sssw2;
                            }
                        }
                    }
                    r_sc_sol_sort_indicator[s][cssw] = 1;
                    sorted_r_sc_sol[cssw].emplace_back(s);
                }
            }
            /*
            for (auto element : sorted_r_sc_sol)
            {
                for (int s : element.second)
                {
                    cout << "r_sc[" << s << "][" << element.first << "]: ";
                    cout << r_sc_sol[s][element.first] << "\n";
                }
            }
            */
            //---------------End of sorting r_sc_sol according to bit rate

            // int sssw_counter = 0; // KALDIRILACAK
            int count_w_s_cl_0s = 0;
            cout << "requests_qty: " << requests_qty << "\n";
            for (auto c_s : sorted_r_sc_sol)
            {
                for (int s_sssw : c_s.second)
                {
                    // cout << "s_sssw: " << s_sssw << " --- *c_s.second.rbegin(): " << *(c_s.second.rbegin()) << "\n";
                    if (s_sssw == *(c_s.second.rbegin()))
                        break; // last sssw is not traveresed so it's ws will be assigned with MILP to keep QoE fairness properties.
                    int s_cssw = c_s.first;
                    auto sssw_itr = net_topo.ServerSideOFSWs_Connected_Servers.find(s_sssw + srv_qty);

                    if (r_sc_sol[s_sssw][s_cssw] == 0)
                    { // if there is no data to send from this sssw to cssw
                        for (int c = 0; c < requests_qty; c++)
                        {
                            // find c's cssw
                            // int cssw = net_topo.client_ip_con_sw_e_index[requests[c]->get_endpoint().address().to_string()] - srv_qty - net_topo.OF_SWs.size(); // client's r_sc index which client connected
                            int cssw = 0;
                            if (cssw == s_cssw) // client'ın bağlı olduğu sw ile işlem yapılan sw (s_cssw) aynı ise
                            {
                                // cout << "cssw == s_cssw\n";
                                layer_qty = m_c;
                                for (int l = 0; l < layer_qty; l++)
                                {
                                    for (auto s : sssw_itr->second) // iterating connected servers at this sssw
                                    {
                                        w_s_cl[c][l][s].setBounds(0, 0);
                                        count_w_s_cl_0s++;
                                        // cout << "-----------------------------setBounds(0, 0): w_s_cl[" << c << l << s << "]\n";
                                    }
                                }
                            }
                        }
                    }
                    else //// if there is some data to send from this sssw to cssw
                    {
                        sending_sssws.emplace(s_sssw);
                        // finds gamma usage percentage
                        double gamma_usage_percent = 0.0;
                        double total_data_rate = 0.0;
                        for (int cssw = 0; cssw < cssw_qty; cssw++)
                        {
                            total_data_rate += r_sc_sol[s_sssw][cssw];
                        }
                        gamma_usage_percent = r_sc_sol[s_sssw][s_cssw] / total_data_rate;
                        // cout << "gamma_usage_percent 1: " << gamma_usage_percent << "\n";

                        // finds total gamma
                        auto i = net_topo.Server_OF_SWs_Connections.find(s_sssw + net_topo.srv_qty);
                        double total_gamma = 0;
                        if (i != net_topo.Server_OF_SWs_Connections.end())
                        {
                            for (auto j : i->second)
                            {
                                total_gamma += gamma_ij_sol[i->first][j];
                            }
                        }

                        layer_qty = m_c;
                        double total_fixed_r_sc = 0;
                        for (int l = 0; l < layer_qty; l++)
                        {
                            for (int c = 0; c < requests_qty; c++)
                            {
                                // int cssw = net_topo.client_ip_con_sw_e_index[requests[c]->get_endpoint().address().to_string()] - srv_qty - net_topo.OF_SWs.size(); // client's r_sc index which client connected
                                int cssw = 0;
                                // srv_qty + sw_qty - 1
                                if (cssw == s_cssw) // client'ın bağlı olduğu sw ile işlem yapılan sw (s_cssw) aynı ise
                                {
                                    for (auto s : sssw_itr->second)
                                    {
                                        // cout << "++++++++++++++++in else condition --- srv: " << s << "-->" << "sssw: " << sssw_itr->first - srv_qty << "\n";
                                        bool w_x_cl_is_set = false;
                                        for (int srv = 0; srv < srv_qty; srv++)
                                        {
                                            if (w_s_cl[c][l][srv].getLB() == 1 && w_s_cl[c][l][srv].getUB() == 1)
                                            {
                                                w_x_cl_is_set = true;
                                                break;
                                            }
                                        }
                                        if (!w_x_cl_is_set) // setBound(1,1) yapılmamışsa (1,1) yapılıyor.
                                        {
                                            double buffer_priority = 1.0;
                                            /*
                                            if (requests[c]->get_buffer() == 0)
                                            {
                                                buffer_priority = 1.2;
                                            }
                                            else if (requests[c]->get_buffer() == 1)
                                            {
                                                buffer_priority = 1.05;
                                            }
                                            */
                                            total_fixed_r_sc += buffer_priority * b_bar_cl[c][l];
                                            if (total_fixed_r_sc <= r_sc_sol[s_sssw][s_cssw])
                                            {
                                                w_s_cl[c][l][s].setBounds(1, 1);
                                                if (counter == 0)
                                                    r_sc_w_s_cl_count[s_sssw][s_cssw]++;
                                            }
                                            else
                                            {
                                                w_s_cl[c][l][s].setBounds(0, 0);
                                                count_w_s_cl_0s++;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                } // End of for(int sssw : c_s.second) --- to traverse all sssw for each cssw

                auto last_s_sssw = *c_s.second.rbegin();
                int s_cssw = c_s.first;
                // auto sssw_itr = net_topo.ServerSideOFSWs_Connected_Servers.find(last_s_sssw + srv_qty);
                sending_sssws.emplace(last_s_sssw);
                IloExpr last_sssw_expr(masterEnv);

                // finds gamma usage percentage
                double gamma_usage_percent = 0.0;
                double total_data_rate = 0.0;
                for (int cssw = 0; cssw < cssw_qty; cssw++)
                {
                    total_data_rate += r_sc_sol[last_s_sssw][cssw];
                }
                gamma_usage_percent = r_sc_sol[last_s_sssw][s_cssw] / total_data_rate;
                // cout << "gamma_usage_percent 2: " << gamma_usage_percent << "\n";

                // finds total gamma
                auto i = net_topo.Server_OF_SWs_Connections.find(last_s_sssw + net_topo.srv_qty);
                double total_gamma = 0;
                if (i != net_topo.Server_OF_SWs_Connections.end())
                {
                    for (auto j : i->second)
                    {
                        total_gamma += gamma_ij_sol[i->first][j];
                    }
                }

                for (int c = 0; c < requests_qty; c++)
                {
                    //  find c's cssw
                    // int cssw = net_topo.client_ip_con_sw_e_index[requests[c]->get_endpoint().address().to_string()] - srv_qty - net_topo.OF_SWs.size(); // client's r_sc index which client connected
                    int cssw = 0;
                    if (cssw == s_cssw)
                    {
                        layer_qty = m_c;
                        for (int l = 0; l < layer_qty; l++)
                        {
                            auto sssw_itr = net_topo.ServerSideOFSWs_Connected_Servers.find(last_s_sssw + srv_qty);
                            for (auto srv : sssw_itr->second)
                            {
                                bool w_x_cl_is_set = false;
                                for (int s = 0; s < srv_qty; s++)
                                {
                                    if (w_s_cl[c][l][s].getLB() == 1 && w_s_cl[c][l][s].getUB() == 1)
                                    {
                                        w_x_cl_is_set = true;
                                        break;
                                    }
                                }
                                if (!w_x_cl_is_set) // kalan w_s_cl'ler için expression giriliyor.
                                {
                                    // cout << "last s - w_s_cl[" << srv << c << l << "] expression written\n";
                                    double buffer_priority = 1.0;
                                    /*
                                    if (requests[c]->get_buffer() == 0)
                                    {
                                        buffer_priority = 1.2;
                                    }
                                    else if (requests[c]->get_buffer() == 1)
                                    {
                                        buffer_priority = 1.05;
                                    }
                                    */
                                    last_sssw_expr += w_s_cl[c][l][srv] * buffer_priority * b_bar_cl[c][l];
                                    // if (counter == 0)
                                    //   r_sc_w_s_cl_count[last_s_sssw][s_cssw]++;
                                }
                            }
                        }
                    }
                }
                cout << "r_sc_sol[" << last_s_sssw << "][" << s_cssw << "]" << r_sc_sol[last_s_sssw][s_cssw] << "\n";
                masterMod.add(last_sssw_expr <= r_sc_sol[last_s_sssw][s_cssw]);
                // masterMod.add(last_sssw_expr <= r_sc_sol[last_s_sssw][s_cssw] + ((total_gamma / 2) * gamma_usage_percent));
                last_sssw_expr.end();
                /*
                for (int s = 0; s < sssw_qty; s++)
                {
                    for (int c = 0; c < cssw_qty; c++)
                    {
                        cout << "r_sc_w_s_cl_count[" << s << "]"<< "[" << c << "]: " << r_sc_w_s_cl_count[s][c] << "\n";
                    }
                }
                */

            } // End of for(auto c_s : sorted_r_sc_sol) --- to traverse all cssw
        } // End of if counter == 0

        masterMod.add(IloMinimize(masterEnv, 30 * Q + (3 * I_cs + 7 * N_cs) / (double)requests_qty)); // OBJ FUNC - 30 client 1 server genelde bununla aldık

        // master problem constraint (0)
        for (int i = 0; i < requests_qty; i++)
        { // each client (request)
            IloExpr master_const0_expr(masterEnv);

            for (int k = 0; k < net_topo.srv_qty; k++)
            {
                master_const0_expr += w_s_cl[i][0][k];
            }
            masterMod.add(master_const0_expr == 1);
            master_const0_expr.end();
        } // end of master problem constrain (0)

        // master problem constraint (1)
        for (int i = 0; i < requests_qty; i++)
        { // each client (request)
            // layer_qty = m_c;
            layer_qty = m_c;
            for (int j = 0; j < layer_qty; j++)
            {                                               // each layer of request
                masterMod.add(masterConst1_RangeArr[i][j]); // CONST 1
            }
        } // end of master problem constrain (1)

        // master problem constraint (2)
        for (int i = 0; i < requests_qty; i++)
        { // each client (request)
            layer_qty = m_c;
            // layer_qty = b_bar_cl[i].size();
            for (int j = 0; j < layer_qty; j++)
            { // each layer of request
                for (int k = 0; k < net_topo.srv_qty; k++)
                { // each server - e array's first srv_qty element contains servers. so loop starts from 0
                    masterMod.add(masterConst2_RangeArr[i][j][k]); 
                }
            }
        } // end of master problem constrain (2)

        // master problem constraint (3)
        for (int i = 0; i < requests_qty; i++)
        { // each client (request)
            layer_qty = m_c;
            // layer_qty = b_bar_cl[i].size();
            for (int j = 0; j < layer_qty - 1; j++)
            { // each layer of request
                masterMod.add(masterConst3_RangeArr[i][j]);
            }
        } // end of master problem constrain (3)

        // master problem constrain (4)
        for (int i = 0; i < requests_qty; i++)
        { // each client (request)
            masterMod.add(masterConst4_RangeArr1[i]);
            // masterMod.add(masterConst4_RangeArr2[i]);
        } // master problem constrain (4)

        // master problem constrain (5)
        for (int i = 0; i < requests_qty; i++)
        { // each client (request)
            masterMod.add(masterConst5_RangeArr[i]);
        } // master problem constrain (5)

        // master problem constrain (6)
        for (int i = 0; i < requests_qty; i++)
        { // each client (request)
            if (segment_index != 0)
            {
                try
                {
                    masterMod.add(masterConst6_RangeArr[i]);
                }
                catch (...)
                {
                    cout << "exeption throw - Contraint 6 !\n";
                }
                // if(masterConst6_RangeArr[i]) masterMod.add(masterConst6_RangeArr[i]);
            }
        } // master problem constrain (6)

        // master problem constrain (7)
        for (int i = 0; i < requests_qty; i++)
        { // each client (request)
            if (segment_index != 0)
            {
                try
                {
                    masterMod.add(masterConst7_RangeArr1[i]);
                    masterMod.add(masterConst7_RangeArr2[i]);
                }
                catch (...)
                {
                    cout << "*********************************************************exeption throw - Contraint 7 - 1 or 2 !\n";
                }
            }
        } // master problem constrain (7)

        // master problem constraint (8) Long term QoE Fairness Constraint
        for (int i = 0; i < requests_qty; i++)
        { // each client (request)
            masterMod.add(masterConst8_RangeArr[i]);
        }
        // masterConst8_RangeArr.end();
        //  End of master problem constraint (8) Long term QoE Fairness Constraint

        IloCplex masterCplex(masterMod);
        masterCplex.setOut(masterEnv.getNullStream()); // Disable CPLEX logging
        masterCplex.setWarning(masterEnv.getNullStream());
        masterCplex.setParam(IloCplex::Param::TimeLimit, 1.0);


    // 1. Set MIP strategy to traditional B&B (faster for small trees)
    masterCplex.setParam(IloCplex::Param::MIP::Strategy::Search, 1);  // 1 = Traditional
    
    // 2. Set optimality gap tolerance to 1%
    masterCplex.setParam(IloCplex::Param::MIP::Tolerances::MIPGap, 0.01);
    
    // 3. Balanced optimality/feasibility emphasis
    masterCplex.setParam(IloCplex::Param::Emphasis::MIP, 1);  // 1 = Balanced
    
    // 4. Enable aggressive presolve
    masterCplex.setParam(IloCplex::Param::Preprocessing::Presolve, 1);  // 1 = On
    //masterCplex.setParam(IloCplex::Param::Preprocessing::Aggregate, 1); // 1 = On
    masterCplex.setParam(IloCplex::Param::Preprocessing::Aggregator, 1); // 1 = On
    
    // 5. Parallel processing (4 threads)
    masterCplex.setParam(IloCplex::Param::Threads, 4);
    
    // 6. Enable strong branching
    masterCplex.setParam(IloCplex::Param::MIP::Strategy::VariableSelect, 3);  // 3 = Strong
    
    // 7. Set solution pool intensity (finds good solutions faster)
    masterCplex.setParam(IloCplex::Param::MIP::Limits::Populate, 10);
    masterCplex.setParam(IloCplex::Param::MIP::Pool::Intensity, 2);
    
    // 8. Enable RINS heuristic (finds integer solutions faster)
    masterCplex.setParam(IloCplex::Param::MIP::Strategy::RINSHeur, 50);
    
    // 9. Additional tuning for large-scale problems
    masterCplex.setParam(IloCplex::Param::MIP::Strategy::Probe, 3);  // Aggressive probing
    masterCplex.setParam(IloCplex::Param::MIP::Cuts::MIRCut, 2);     // Aggressive MIR cuts
    masterCplex.setParam(IloCplex::Param::MIP::Cuts::FlowCovers, 2); // Flow cover cuts


        if (masterCplex.solve())
        {
            masterCplex.exportModel("masterModel.lp");
            master_solved = true;

            IloAlgorithm::Status solStatus = masterCplex.getStatus();
            masterEnv.out() << "Master Solution status: " << solStatus << endl;
            // masterEnv.out() << "Master Objective value: " << masterCplex.getObjValue() << endl;

            // copy & print w_s_cl values

            for (int i = 0; i < requests_qty; i++)
            {
                layer_qty = m_c;
                // w_s_cl_sol[i] = IloNumArray2(masterEnv, layer_qty);
                for (int j = 0; j < layer_qty; j++)
                {
                    // w_s_cl_sol[i][j] = IloNumArray(masterEnv, net_topo.srv_qty);
                    masterCplex.getValues(w_s_cl[i][j], w_s_cl_sol[i][j]);
                    // masterEnv.out() << "w result in master: " << w_s_cl_sol[i][j] << "\n";
                }

                // masterCplex.getValue(v_c[i], v_c_sol[i]);

                if (segment_index != 0)
                {
                    masterCplex.getValue(v_c[i], v_c_sol[i]);
                }
            }

            // Updates r_sc_w_s_cl_count of biggest data sender r_sc
            if (counter == 0)
            {
                for (auto c_s : sorted_r_sc_sol)
                {
                    auto last_s_sssw = *c_s.second.rbegin();
                    int last_s_cssw = c_s.first;
                    for (int s = 0; s < srv_qty; s++)
                    {
                        int sssw = net_topo.srv_con_sws_e_index2[s] - srv_qty;
                        for (int c = 0; c < requests_qty; c++)
                        {
                            // int cssw = net_topo.client_ip_con_sw_e_index[requests[c]->get_endpoint().address().to_string()] - srv_qty - net_topo.OF_SWs.size(); // client's r_sc index which client connected
                            int cssw = 0;
                            if (last_s_sssw == sssw && last_s_cssw == cssw)
                            {
                                for (int l = 0; l < layer_qty; l++)
                                {
                                    if (w_s_cl_sol[c][l][s] == 1)
                                    {
                                        r_sc_w_s_cl_count[sssw][cssw]++;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // cout << "Updated r_sc_w_s_cl_count\n";
            for (int s = 0; s < sssw_qty; s++)
            {
                for (int c = 0; c < cssw_qty; c++)
                {
                    // cout << "r_sc_w_s_cl_count[" << s << "]"<< "[" << c << "]: " << r_sc_w_s_cl_count[s][c] << "\n";
                    if (r_sc_w_s_cl_count[s][c] > 0)
                        senders_qty[c]++;
                }
            }

            total_w_s_cl_result = 0;

            for (int i = 0; i < requests_qty; i++)
            {
                video_quality.emplace_back(vector<int>());
            }

            // int w_result_k = 0;
            for (int i = 0; i < requests_qty; i++)
            {
                int w_result_i = 0;
                layer_qty = m_c;
                for (int j = 0; j < layer_qty; j++)
                {
                    for (int k = 0; k < net_topo.srv_qty; k++)
                    {
                        total_w_s_cl_result += w_s_cl_sol[i][j][k];
                        w_result_i += w_s_cl_sol[i][j][k];
                    }
                    // cout << "client " << requests[i]->get_endpoint().address().to_string() << "'s w result in master from server "<< k << ": " << w_result_i << "\n";
                    // w_result_k += w_result_i;
                }
                video_quality[i].emplace_back(w_result_i);
                // cout << " w results from server " << k << ": " << w_result_k << "\n";
            }

            // cout << "------*****senders_qty: " << senders_qty[0] << "\n";
            /*
            for (int k = 0; k < net_topo.srv_qty; k++)
            {
                int w_result_k = 0;
                for (int i = 0; i < requests_qty; i++)
                {
                    int w_result_i = 0;
                    layer_qty = m_c;
                    for (int j = 0; j < layer_qty; j++)
                    {
                        w_result_i += w_s_cl_sol[i][j][k];
                    }
                    cout << "client " << requests[i]->get_endpoint().address().to_string() << "'s w result in master from server "<< k << ": " << w_result_i << "\n";
                    w_result_k += w_result_i;
                }
                cout << " w results from server " << k << ": " << w_result_k << "\n";
            }
            */
        }
        else
        {
            master_solved = false;
            masterEnv.out() << "Master: No solution available" << endl;
        }
        // Close the environment
    }
    catch (const IloException &e)
    {
        cerr << "Exception caught: " << e << endl;
        // Close the environment
    }
    catch (...)
    {
        cerr << "Unknown exception caught!" << endl;
        // Close the environment
    }
}
// end of master problem

void optimizer()
{
    Net_Topo net_topo;
    int interval = 2000;
    int const m_c = 4; // max layer m_c
    double teta = 2.0; // buffering time. Download duration.
    unordered_map<string, double> files_sizes(net_topo.requests_qty);
    get_video_file_sizes(files_sizes);
    int segment_qty = files_sizes.size() / m_c;
    // phi_c (total number of requested segment by client c) is one of the value which is used in constraint 5 in master
    int phi_c = 0;
    int priority = 0;

    // set<string> optimizers = {"10.0.0.100"}; // server ip addresses
    // set<string> servers = {"10.0.0.200"};    // server ip addresses
    // int srv_qty = servers.size();
    // int optimizer_qty = optimizers.size();
    // int hosts_qty = net_topo.requests_qty + srv_qty + optimizer_qty;
    // int client_qty = net_topo.requests_qty;
    // int sw_qty = 6;
    // int vertex_qty = hosts_qty + sw_qty - optimizer_qty;

    int cssw_qty = net_topo.ClientSideOFSWs.size();
    int sssw_qty = net_topo.ServerSideOFSWs.size();
    // cout << "cssw_qty : " << cssw_qty << "\n";
    // cout << "sssw_qty : " << sssw_qty << "\n";
    /*
    for (int i = 0; i < segment_qty; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            std::string file_name = "BBB-I-1080p.seg" + std::to_string(i) + "-L" + std::to_string(j) + ".svc";
            cout << file_name << " : " << files_sizes[file_name] << "\n";
        }
    }
    */

    auto now = std::chrono::steady_clock::now();
    auto next = now + std::chrono::milliseconds(interval);

    Json::Reader reader;
    Json::Value json_flows;
    Json::Value json_flow_srv_dst;
    Json::Value json_flow_srv_src;
    Json::Value json_message;
    Json::Value json_messages;
    // Json::Value json_drop_message;
    std::string text_messages = R"({"msgs":[]})";
    std::string text_message = R"({"layer":0, "server_ip":"", "tcp_port":8000})";
    // std::string text_drop_message = R"({"drop":0)";

    std::string text_flows = R"({"flows":[]})";
    std::string text_flow = R"(                           
        {
        "priority": 5000, 
        "timeout": 10,  
        "isPermanent": false, 
        "deviceId": "",
        "tableId": 0, 
        "treatment": { "instructions": [ { "type": "OUTPUT", "port": 0}] }, 
        "selector": { 
                "criteria": [
                {"type": "ETH_TYPE", "ethType": "0x0800"},
                {"type":"IPV4_SRC", "ip":""},
                {"type":"IPV4_DST", "ip":""},
                {"type": "IP_PROTO", "protocol": 6},
                {"type": "", "tcpPort": 0}
                ]
            }
        }
        )";

    // cout << "segment_qty: " << segment_qty << "\n";
    for (int segment_index = 0; segment_index < segment_qty; segment_index++)
    {

        cout << "\n----------------------------------NEW OPT CYCLE STARTED----------------------------------------------\n";
        std::this_thread::sleep_until(next);
        now = std::chrono::steady_clock::now();
        next = now + std::chrono::milliseconds(interval);

        // Calculate the difference
        auto difference = next - now;
        // Convert the difference to milliseconds
        auto diff_in_ms = std::chrono::duration_cast<std::chrono::milliseconds>(difference).count();
        // cout << "segment " << segment_index << " diff_in_ms: " << diff_in_ms << "\t";

        // b_bar_cl definitions (file_size/teta vector). Required BW for layer l of client c.
        vector<vector<double>> b_bar_cl(net_topo.requests_qty);                               // size of layers requested by client c
        set_b_bar_cl(b_bar_cl, net_topo.requests_qty, m_c, files_sizes, teta, segment_index); // sets b_bar_cl which contains layers size ( required byte per sec to download in teta time)
        phi_c++;

        IloEnv masterEnv;
        IloEnv multiserverEnv;
        IloIntVarArray3 w_s_cl(masterEnv, net_topo.requests_qty);  // whether server s serves layer l to client c
        IloNumArray3 w_s_cl_sol(masterEnv, net_topo.requests_qty); // result of optimization of w_s_cl
        for (int i = 0; i < net_topo.requests_qty; i++)            // Initilization of w_s_cl
        {
            int layer_qty = m_c;
            w_s_cl_sol[i] = IloNumArray2(masterEnv, layer_qty);
            for (int j = 0; j < layer_qty; j++)
            {
                w_s_cl_sol[i][j] = IloNumArray(masterEnv, net_topo.srv_qty);
            }
        }

        IloIntArray v_c_sol(masterEnv, net_topo.requests_qty); // stores v_c result to use it in v_bar_c for next optimizations.

        // IloBool solution_found = IloFalse;
        IloBool solution_found = IloTrue;
        int total_w_s_cl_max = total_layer_qty(b_bar_cl, net_topo.requests_qty);

        int total_w_s_cl_ub = total_w_s_cl_max;
        int last_feas_total_w_s_cl = 0;
        int last_infeas_total_w_s_cl = total_w_s_cl_max;

        IloNumVar Q(masterEnv, 0, 1);
        IloNumVar L(masterEnv, 0, 1);
        IloNumVarArray T_c(masterEnv, net_topo.requests_qty, 0, 1);
        IloNumVarArray I_c(masterEnv, net_topo.requests_qty, 0, 1);
        IloIntVarArray v_c(masterEnv, net_topo.requests_qty, 0, 1);
        IloNumVarArray N_c(masterEnv, net_topo.requests_qty, 0, 1);

        IloExpr masterOptConstExpr(masterEnv);

        IloArray<IloRangeArray> masterConst1_RangeArr(masterEnv, net_topo.requests_qty);
        IloArray<IloArray<IloRangeArray>> masterConst2_RangeArr(masterEnv, net_topo.requests_qty);
        IloArray<IloRangeArray> masterConst3_RangeArr(masterEnv, net_topo.requests_qty);
        IloRangeArray masterConst4_RangeArr1(masterEnv, net_topo.requests_qty);
        IloRangeArray masterConst4_RangeArr2(masterEnv, net_topo.requests_qty);
        IloRangeArray masterConst5_RangeArr(masterEnv, net_topo.requests_qty);
        IloRangeArray masterConst6_RangeArr(masterEnv, net_topo.requests_qty);
        IloRangeArray masterConst7_RangeArr1(masterEnv, net_topo.requests_qty);
        IloRangeArray masterConst7_RangeArr2(masterEnv, net_topo.requests_qty);
        IloRangeArray masterConst8_RangeArr(masterEnv, net_topo.requests_qty);
        IloRangeArray master_FeasCutArray(masterEnv);
        IloNumArray2 r_sc_sol(multiserverEnv, sssw_qty);
        IloNumArray2 r_sc_gamma_sol(multiserverEnv, sssw_qty);
        IloNumArray4 f_sc_ij_sol(multiserverEnv, sssw_qty);

        IloNumArray2 gamma_ij_sol(multiserverEnv, (net_topo.srv_qty + net_topo.sw_qty));
        // std::map<int, double> req_max_rates_from_cssws;  // keeps required max data rate for clients site sws
        std::map<int, double> req_max_rates_from_cssws; // keeps required max data rate for clients site sws

        std::map<int, std::vector<int>> sorted_r_sc_sol; // int is cssw, vector is sending sssw
        std::set<int> sending_sssws;                     // keeps all data sending servers
        std::vector<std::vector<int>> combinations;      // keeps all combinations of sending_sssws of r_value
        int r_value = 1;
        int nCr_counter = 0;
        int addition_to_sub_layer = 0;
        bool need_inc_add_sub_layer = false;
        int inc_cancelled = 0;
        vector<double> provided_rate_for_c(cssw_qty);

        for (int s = 0; s < sssw_qty; s++)
        {
            r_sc_sol[s] = IloNumArray(multiserverEnv, cssw_qty);
            r_sc_gamma_sol[s] = IloNumArray(multiserverEnv, cssw_qty);
        }

        for (int i = net_topo.srv_qty; i < (net_topo.srv_qty + net_topo.sw_qty); i++)
        {
            gamma_ij_sol[i] = IloNumArray(multiserverEnv, (net_topo.srv_qty + net_topo.sw_qty));
        }

        int a_s_cl = 1;

        auto opt_start_time = std::chrono::steady_clock::now();
        // multiserver(multiserverEnv, net_topo, b_bar_cl, net_topo.requests_qty, r_sc_sol, r_sc_gamma_sol, req_max_rates_from_cssws, gamma_ij_sol, provided_rate_for_c);
        //cout << "multiserver starts\n";
        multiserver(multiserverEnv, net_topo, b_bar_cl, m_c, r_sc_sol, r_sc_gamma_sol, gamma_ij_sol, provided_rate_for_c, f_sc_ij_sol);
        //cout << "multiserver ends\n";

        masterInitBuilder(masterEnv, w_s_cl, Q, L, T_c, I_c, v_c, N_c, net_topo.requests_qty, b_bar_cl, net_topo, m_c, a_s_cl, masterOptConstExpr, masterConst1_RangeArr, masterConst2_RangeArr,
                          masterConst3_RangeArr, masterConst4_RangeArr1, masterConst4_RangeArr2, masterConst5_RangeArr, masterConst6_RangeArr, masterConst7_RangeArr1, masterConst7_RangeArr2,
                          masterConst8_RangeArr, phi_c);
        bool worst_case = false;
        bool master_solved = false;
        int counter = 0;
        bool dec_buff_for_master = false;
        int total_w_s_cl_result = 0;
        int total_w_s_cl_sol = 0;

        vector<vector<int>> r_sc_w_s_cl_count(net_topo.ServerSideOFSWs.size(), vector<int>(net_topo.ClientSideOFSWs.size())); // used to keep number of w send from each r_sc

        if (!worst_case)
        {
            master(masterEnv, w_s_cl, w_s_cl_sol, Q, L, T_c, I_c, v_c, v_c_sol, N_c, net_topo.requests_qty, b_bar_cl, net_topo, m_c, a_s_cl, masterOptConstExpr, masterConst1_RangeArr, masterConst2_RangeArr,
                   masterConst3_RangeArr, masterConst4_RangeArr1, masterConst4_RangeArr2, masterConst5_RangeArr, masterConst6_RangeArr, masterConst7_RangeArr1, masterConst7_RangeArr2, masterConst8_RangeArr, total_w_s_cl_ub, total_w_s_cl_max,
                   r_sc_sol, r_sc_gamma_sol, counter, req_max_rates_from_cssws, gamma_ij_sol, r_sc_w_s_cl_count, master_FeasCutArray, sorted_r_sc_sol,
                   sending_sssws, combinations, nCr_counter, r_value, addition_to_sub_layer, need_inc_add_sub_layer, inc_cancelled, provided_rate_for_c, dec_buff_for_master, total_w_s_cl_result,
                   master_solved, last_infeas_total_w_s_cl, total_w_s_cl_sol, segment_index);
        }


        auto flow_assignment_start_time = std::chrono::steady_clock::now();
        //cout << "Flow assignment starts\n";
        if (solution_found)
        {

            // reader.parse(text_message, json_message);
            // reader.parse(text_messages, json_messages);

            std::vector<int> w_result_i(net_topo.requests_qty);
            std::vector<int> w_result_k(net_topo.srv_qty);
            for (int k = 0; k < net_topo.srv_qty; k++)
            {
                for (int i = 0; i < net_topo.requests_qty; i++)
                {
                    int w_result_for_k = 0;
                    int layer_qty = m_c;
                    for (int j = 0; j < layer_qty; j++)
                    {
                        w_result_i[i] += w_s_cl_sol[i][j][k]; // i=client, j=layer, k=server
                        w_result_for_k += w_s_cl_sol[i][j][k];
                    }
                    // cout << "client " << requests[i]->get_endpoint().address().to_string() << "'s w result in SOLUTION FOUND from server "<< k << ": " << w_result_i[i] << "\n";
                    w_result_k[k] += w_result_for_k;
                }
                // cout << " w results in SOLUTION FOUND from server " << k << ": " << w_result_k[k] << "\n";
            }
            std::vector<std::vector<int>> usage_in_flows(net_topo.srv_qty + net_topo.sw_qty, std::vector<int>(net_topo.srv_qty + net_topo.sw_qty, 0));
            json_flow_srv_src["priority"] = ++priority;
            int flow_counter = 0;
            json_flows.clear();
            json_messages.clear();
            //cout << "flow assignment start\n";
            // Flow assingments, starting from least available capacity owner switch.
            for (auto c_s : sorted_r_sc_sol)
            {
                for (int s_sssw : c_s.second)
                {
                    int s_cssw = c_s.first;                                     // in Y
                    int y = s_cssw + net_topo.srv_qty + net_topo.OF_SWs.size(); // e index of client site switch
                    int x = s_sssw + net_topo.srv_qty;                          ////e index of server site switch
                    auto sssw_itr = net_topo.ServerSideOFSWs_Connected_Servers.find(s_sssw + net_topo.srv_qty);
                    std::string srv_ip = net_topo.srv_e_index_ip[s_sssw];

                    // if there is some data to send from this sssw to cssw
                    if (r_sc_sol[s_sssw][s_cssw] > 0)
                    {
                        int layer_qty = m_c;
                        double total_fixed_r_sc = 0;
                        for (int l = 0; l < layer_qty; l++)
                        {
                            for (int c = 0; c < net_topo.requests_qty; c++)
                            {
                                if (w_s_cl_sol[c][l][s_sssw] == 1)
                                {
                                    // cout << "layer: " << l << " - client: " << c << "\n";
                                    // json_flow_srv_dst["selector"]["criteria"][4]["type"] = "TCP_DST";
                                    // json_flow_srv_dst["selector"]["criteria"][4]["tcpPort"] = TCP_PORTS[port_change_flag][layer]; // port_change_flag variable fixed as 0. Bacause I deciced to use only one TCP port set on server side as consequence of deciding that clients send sequential http requests to servers.
                                    json_flow_srv_src["selector"]["criteria"][4]["type"] = "TCP_SRC";
                                    json_flow_srv_src["selector"]["criteria"][4]["tcpPort"] = 8000 + l;

                                    std::string client_ip = "10." + std::string("1.") + std::to_string(c / 256) + "." + std::to_string(c % 256);

                                    int current_sw = x;
                                    int sw_counter = 0;
                                    while (current_sw != y)
                                    {
                                        // cout << "current_sw("<<current_sw<<") --- " <<"y("<<y<<")\n";
                                        //  cout << "current_sw != y: " << current_sw <<   " != " << y << "\n";

                                        for (int next_sw : net_topo.OF_SWs_Connections[current_sw])
                                        {
                                            // cout << "next_sw: " <<next_sw <<"\n";
                                            double buffer_priority = 1.0;
                                            if (f_sc_ij_sol[s_sssw][s_cssw][current_sw][next_sw] - buffer_priority * b_bar_cl[c][l] >= 0.0)
                                            {
                                                // cout << "f_sc_ij_sol["<<s_sssw<<"]["<<s_cssw<<"]["<<current_sw<<"]["<< next_sw<<"]: "<< f_sc_ij_sol[s_sssw][s_cssw][current_sw][next_sw] <<"\n";
                                                //  cout << "current_sw - next_sw: " << current_sw << " - " << next_sw << "\n";
                                                f_sc_ij_sol[s_sssw][s_cssw][current_sw][next_sw] -= buffer_priority * b_bar_cl[c][l];

                                                usage_in_flows[current_sw][next_sw] += buffer_priority * b_bar_cl[c][l];

                                                // net_topo.total_b_bar_cl_at_t_1_on_ij[current_sw][next_sw] = usage_in_flows[current_sw][next_sw];

                                                json_flow_srv_src["deviceId"] = "of:"; //+ net_topo.sw_e_index_id.find(current_sw)->second;         // DeviceId is added to flow text
                                                // cout << "f_sc_ij_sol["<<s_sssw<<"]["<<s_cssw<<"]["<<current_sw<<"]["<< next_sw<<"]: "<< f_sc_ij_sol[s_sssw][s_cssw][current_sw][next_sw] <<"\n";

                                                json_flow_srv_src["treatment"]["instructions"][0]["port"] = "1";        // net_topo.ports[current_sw][next_sw]; // output port added
                                                json_flow_srv_src["selector"]["criteria"][1]["ip"] = srv_ip + "/32";    // IPV4_SRC - Server IP
                                                json_flow_srv_src["selector"]["criteria"][2]["ip"] = client_ip + "/32"; // IPV4_DST - Client IP
                                                json_flows["flows"][flow_counter++] = json_flow_srv_src;
                                                current_sw = next_sw;
                                                break;
                                            }
                                        }
                                        if (++sw_counter >= net_topo.sw_qty)
                                            break;
                                    }
                                }
                            }
                        }
                    }
                } // End of for(int sssw : c_s.second) --- to traverse all s for each c
            } // End of for(c_s : sorted_r_sc_sol) --- to traverse all c
            //cout << "flow assignment end\n";

            Json::FastWriter fastWriter;
            std::string flows = fastWriter.write(json_flows);
            // net_topo.add_flow(flows);

            //cout << "json messages - start\n";
            for (int i = 0; i < net_topo.requests_qty; ++i)
            {
                std::string client_ip = "10." + std::string("1.") + std::to_string(i / 256) + "." + std::to_string(i % 256);

                // string client_ip = requests[i]->get_endpoint().address().to_string(); // Client IP
                int w_s_c_l_sol_for_i = 0; // result of optimization of layer quality for c's requested segment
                int layer_qty = m_c;
                // layer_qty = b_bar_cl[i].size();
                for (int j = 0; j < layer_qty; ++j)
                {
                    // w_s_c_l_sol_for_i += IloSum(w_s_cl_sol[i][j]);

                    for (int k = 0; k < net_topo.srv_qty; k++)
                    {
                        w_s_c_l_sol_for_i += w_s_cl_sol[i][j][k];
                    }
                }

                json_messages.clear();

                mu_bar_c[i] += abs(w_s_c_l_sol_for_i - l_bar_c[i]); // used in contraint 6 in master - layer switch intensity
                v_bar_c[i] += v_c_sol[i];                           // used in contraint 7 in master - layer switch
                l_bar_c[i] = w_s_c_l_sol_for_i;                     // used in contraint 6 in master. Previous time slot achived layers. This var will be used in next opt calculation
                lambda_bar_c[i] += w_s_c_l_sol_for_i;               // used in contraint 5 in master

                for (int layer = 0; layer < w_s_c_l_sol_for_i; ++layer)
                {
                    json_message["layer"] = layer; // message to client for layer info
                    json_message["tcp_port"] = (8000 + layer);

                    int current_sw_index;
                    string srv_ip;
                    for (int s = 0; s < net_topo.srv_qty; s++)// traverse all servers
                    {                                     
                        if (w_s_cl_sol[i][layer][s] == 1) 
                        {
                            srv_ip = net_topo.srv_e_index_ip[s];
                            // cout << "srv_ip: " << srv_ip << "\n";
                            json_message["server_ip"] = srv_ip; // message prepperation to client
                        }
                    }
                    json_messages["msgs"][layer] = json_message;
                }

                // json_messages["indx"] = requests[i]->get_seg_index();
                json_messages["indx"] = segment_index;
                json_messages["buf"] = 0;
                json_messages["ip"] = client_ip;

                if (w_s_c_l_sol_for_i == 0)
                {
                    string srv_ip;
                    if (srv_ip == "")
                    {
                        auto itr = net_topo.servers.begin();
                        std::advance(itr, (i % net_topo.servers.size()));

                        // string ip = *itr;
                        srv_ip = *itr;
                        // cout << "server ip: " << srv_ip << "\n";
                    }
                    json_message["layer"] = 0; // message to client for layer info
                    json_message["layer_qty"] = 1;
                    json_message["tcp_port"] = 8000;
                    json_message["server_ip"] = srv_ip; // preferred or previous iteration server ip

                    json_messages["msgs"][0] = json_message;
                    // cout << "sol is 0 - json_message: " << json_message << "\n";
                    json_messages["indx"] = segment_index;
                    json_messages["buf"] = 0;
                    json_messages["ip"] = client_ip;
                }

                Json::FastWriter fastWriter;
                std::string messages = fastWriter.write(json_messages); // json to string conversion - message to client{"layer_qty":int, "tcp_port":int}
            } // end of for of requests
            //cout << "json messages - end\n";

            // delete_requests(net_topo.requests_qty); // deletes requests elements till net_topo.requests_qty
        }
        else
        {
            // BU ALANA CLIENT İÇİN DÖNÜŞ BİLGİSİ KODLAMASI YAPILACAK.
            cout << "NO SOLUTION AVAIABLE! --- SENDING BASE LAYER INFO\n";
            cout << "net_topo.srv_qty: " << net_topo.srv_qty << "\n";

            for (int i = 0; i < net_topo.requests_qty; ++i)
            {
                // message preperation to client
                json_messages.clear();
                std::string client_ip = "10." + std::string("1.") + std::to_string(i / 256) + "." + std::to_string(i % 256);

                // string client_ip = requests[i]->get_endpoint().address().to_string(); // Client IP

                mu_bar_c[i] += abs(1 - l_bar_c[i]); // used in contraint 6 in master - layer switch intensity
                v_bar_c[i] += v_c_sol[i];           // used in contraint 7 in master - layer switch
                l_bar_c[i] = 1;
                lambda_bar_c[i] += 1;

                // std::string srv_ip = requests[i]->get_srv_ip();
                std::string srv_ip = "10.0.0.200";

                if (srv_ip == "")
                {
                    auto itr = net_topo.servers.begin();
                    std::advance(itr, (i % net_topo.servers.size()));
                    srv_ip = *itr;
                }
                json_message["layer"] = 0; // message to client for layer info
                json_message["tcp_port"] = 8000;
                json_message["server_ip"] = srv_ip; // preferred or previous iteration server ip

                json_messages["msgs"][0] = json_message;
                json_messages["indx"] = segment_index;
                json_messages["buf"] = 0;
                json_messages["ip"] = client_ip;
                Json::FastWriter fastWriter;
                std::string message = fastWriter.write(json_messages); // json to string conversion - message to client{"layer_qty":int, "tcp_port":int}
                // cout << "requests[" << i << "] message: " << message << " sent!!!\n";
                // requests[i]->post(message);
                // cout << client_ip << " - message sent: " << message << "\n";
            } // end of for of requests
            // delete_requests(requests_qty); // deletes requests elements till requests_qty
        }
        flow_assignment_runtimes.emplace_back((std::chrono::steady_clock::now() - flow_assignment_start_time)); // Optimizer's run time is recorded.

        multiserverEnv.end();
        masterEnv.end();
        optimizer_runtimes.emplace_back((std::chrono::steady_clock::now() - opt_start_time)); // Optimizer's run time is recorded.

    } // End of segment_index loop
    cout << "Video Quality:\n";
    for (int i = 0; i < net_topo.requests_qty; i++)
    {
        for (auto quality : video_quality[i])
        {
            cout << quality << "\t";
        }
        cout << "\n";
    }
    cout << "\n";
    cout << "\n";
    cout << "Optimizer Runtimes:";
    for (auto runtime : optimizer_runtimes)
    {
        cout << runtime.count() << "\t";
    }

    cout << "\n";
    cout << "\n";

    cout << "Flow Assignment Runtimes:";
    for (auto runtime : flow_assignment_runtimes)
    {
        cout << runtime.count() << "\t";
    }
    cout << "\n";

} // End of optimizer()

int main(int argc, char **argv)
{

    optimizer();

    return 0;
}