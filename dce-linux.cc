#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/dce-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/csma-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include <fstream>
using namespace ns3;
#define DEBUG 0

NS_LOG_COMPONENT_DEFINE ("DceLinux");
std::string transProt = "cubic"; // Configure TCP CA algorithm Cubic

class MyApp : public Application
{
public:

        MyApp ();
        virtual ~MyApp();

        void Setup (Ptr<Node> socket, Address address, uint32_t packetSize, uint32_t nPackets, DataRate dataRate);

private:
        virtual void StartApplication (void);
        virtual void StopApplication (void);

        void ScheduleTx (void);
        void SendPacket (void);

        Ptr<Socket>     m_socket;
        Ptr<Node>       m_node;
        Address         m_peer;
        uint32_t        m_packetSize;
        uint32_t        m_nPackets; 
        DataRate        m_dataRate;
        EventId         m_sendEvent;
        bool            m_running;
        uint32_t        m_packetsSent;
};
             
MyApp::MyApp ()
        : m_socket (0), 
          m_peer (),
          m_packetSize (0),
          m_nPackets (0),
          m_dataRate (0),
          m_sendEvent (),
          m_running (false), 
          m_packetsSent (0)
{
}

MyApp::~MyApp()
{
        m_socket = 0;
}

void
MyApp::Setup (Ptr<Node> node, Address address, uint32_t packetSize, uint32_t nPackets, DataRate dataRate)
{
        m_node = node;
        m_peer = address;
        m_packetSize = packetSize;
        m_nPackets = nPackets;
        m_dataRate = dataRate;
        int64_t switching_time = Simulator::GetSwitchTime(0);
        if (DEBUG) std::cout <<"switching time:" << switching_time << std::endl;

        if (switching_time != 0)
        Simulator::Schedule (NanoSeconds(switching_time), &Simulator::ChangeState); // Scheduled if it applies feedback2
}

void
MyApp::StartApplication (void)
{
        m_running = true;
        m_packetsSent = 0;
        Ptr<Socket> LinuxTcpSocket = Socket::CreateSocket (m_node, LinuxTcpSocketFactory::GetTypeId ());
        m_socket = LinuxTcpSocket;
        m_socket->Bind ();
        m_socket->Connect (m_peer);

        Simulator::Schedule (Seconds(1.5), &MyApp::SendPacket, this); //Now waiting for tcp established then start to send APP packets;

        if (DEBUG) std::cout << "start application" << std::endl;
}

void
MyApp::StopApplication (void)
{
        m_running = false;

        if (m_sendEvent.IsRunning ())
        {
                Simulator::Cancel (m_sendEvent);
        }

        if (m_socket)
        {
                m_socket->Close ();
        }
}

void
MyApp::SendPacket (void)
{
        Ptr<Packet> packet = Create<Packet> (m_packetSize);
        int actual = m_socket->Send (packet);

        if (++m_packetsSent < m_nPackets)
        {
                ScheduleTx ();
        }
}

void
MyApp::ScheduleTx (void)
{
        uint64_t dataRate = Simulator::GetAppSpeed(0) * 1000; //unit 1: kb

        if (m_running)
        {
                Time tNext (Seconds (m_packetSize * 8 / static_cast<double> (dataRate)));
                m_sendEvent = Simulator::Schedule (tNext, &MyApp::SendPacket, this);
        }
}


void
PrintTcpFlags (std::string key, std::string value)
{
        std::cout << key << "=" << value << std::endl;
}

int main (int argc, char *argv[])
{
        CommandLine cmd;
        std::string data_rate = "100Gbps";

        NodeContainer nodes;
        nodes.Create (2);

        NetDeviceContainer devices;

        PointToPointHelper p2p;
        p2p.SetDeviceAttribute ("DataRate", StringValue (data_rate));
        p2p.SetChannelAttribute ("Delay", StringValue ("1ms"));
        devices = p2p.Install (nodes);
        //p2p.EnablePcap ("process-linux", devices, true); // enable pcap file when debug

        DceManagerHelper processManager;
        processManager.SetNetworkStack ("ns3::LinuxSocketFdFactory", "Library", StringValue ("liblinux.so"));
        processManager.Install (nodes);
        LinuxStackHelper stack;
        stack.Install (nodes);

        Ipv4AddressHelper address;
        address.SetBase ("10.0.0.0", "255.255.255.0");
        Ipv4InterfaceContainer interfaces = address.Assign (devices);

        Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
        LinuxStackHelper::PopulateRoutingTables ();

        DceApplicationHelper process;
        ApplicationContainer apps;
        process.SetStackSize (1 << 20);

        //linux parems configuration
        stack.SysctlSet (nodes, ".net.core.wmem_max", "134217728");
        stack.SysctlSet (nodes, ".net.core.rmem_max", "134217728");
        stack.SysctlSet (nodes, ".net.ipv4.tcp_rmem", "4096 87380 67108864");
        stack.SysctlSet (nodes, ".net.ipv4.tcp_wmem", "4096 25000000 67108864"); // changed for application buffer
        stack.SysctlSet (nodes, ".net.ipv4.tcp_mem", "196608 262144 393216");//
        stack.SysctlSet (nodes, ".net.ipv4.tcp_congestion_control", transProt); // done at 0.1 seconds
        stack.SysctlSet (nodes, ".net.ipv4.tcp_no_metrics_save", "0");

        //application configuration
        std::string sock_factory = "ns3::LinuxTcpSocketFactory";
        Address sinkAddress (InetSocketAddress (Ipv4Address ("10.0.0.1"), 2000));
        Ptr<MyApp> app = CreateObject<MyApp> ();
        app->Setup (nodes.Get (1), sinkAddress, 1500, 10000, DataRate ("100Mbps")); // 15 Mb file, hardcoded in the sink application to re
        nodes.Get (1)->AddApplication (app);
        app->SetStartTime (Seconds (0.5));
        //      app->SetStopTime (Seconds (20.)); // Current NS3 will explictly terminate when receiving 15 Mb file


        PacketSinkHelper sink = PacketSinkHelper (sock_factory,
                                InetSocketAddress (Ipv4Address::GetAny (), 2000));
        apps = sink.Install (nodes.Get(0));
        apps.Start (Seconds (0.4));


        Simulator::Stop (Seconds (20000000));
        Simulator::Run ();

        Simulator::Destroy ();

        return 0;
}
        