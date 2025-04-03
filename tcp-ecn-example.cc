/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * TCP/IP Example with ECN support
 *
 * Network topology:
 *
 *    Sender
 *       |
 *       | 10 Mbps, 5 ms
 *       |
 *    Router
 *       |
 *       | 1 Mbps, 20 ms (bottleneck link)
 *       |
 *    Receiver
 *
 * - TCP flow from Sender to Receiver
 * - ECN can be enabled/disabled for all nodes via command line parameter (--enableEcn=true/false)
 * - PCAP files are generated for all nodes when tracing is enabled (--enablePcap=true)
 * - Various queue disciplines can be tested (--queueDiscType="ns3::RedQueueDisc")
 *
 * Example usage:
 * ./ns3 run "scratch/tcp-ecn-example --enableEcn=true --enablePcap=true"
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

#include <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpEcnExample");

int
main(int argc, char* argv[])
{
    bool enableEcn = true;
    bool enablePcap = true;
    bool enableTracing = false;
    uint32_t maxBytes = 0;
    std::string queueDiscType = "ns3::RedQueueDisc";
    uint32_t queueSize = 25;    // packets
    double simulationTime = 10; // seconds

    // Command line arguments
    CommandLine cmd(__FILE__);
    cmd.AddValue("enableEcn", "Enable ECN", enableEcn);
    cmd.AddValue("enablePcap", "Enable PCAP tracing", enablePcap);
    cmd.AddValue("enableTracing", "Enable tracing", enableTracing);
    cmd.AddValue("maxBytes", "Total number of bytes for application to send", maxBytes);
    cmd.AddValue("queueDiscType",
                 "Queue disc type (ns3::RedQueueDisc or ns3::CoDelQueueDisc)",
                 queueDiscType);
    cmd.AddValue("queueSize", "Queue size in packets", queueSize);
    cmd.AddValue("simulationTime", "Simulation time in seconds", simulationTime);
    cmd.Parse(argc, argv);

    // ECN Configuration
    if (enableEcn)
    {
        NS_LOG_INFO("ECN is enabled for all nodes");
        Config::SetDefault("ns3::TcpSocketBase::UseEcn", EnumValue(TcpSocketState::On));
        Config::SetDefault("ns3::RedQueueDisc::UseEcn", BooleanValue(true));
        Config::SetDefault("ns3::CoDelQueueDisc::UseEcn", BooleanValue(true));
    }
    else
    {
        NS_LOG_INFO("ECN is disabled for all nodes");
        Config::SetDefault("ns3::TcpSocketBase::UseEcn", EnumValue(TcpSocketState::Off));
        Config::SetDefault("ns3::RedQueueDisc::UseEcn", BooleanValue(false));
        Config::SetDefault("ns3::CoDelQueueDisc::UseEcn", BooleanValue(false));
    }

    // Select TCP variant
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpNewReno"));

    // Create nodes
    NS_LOG_INFO("Creating nodes");
    NodeContainer nodes;
    nodes.Create(3);

    NodeContainer senderRouter = NodeContainer(nodes.Get(0), nodes.Get(1));
    NodeContainer routerReceiver = NodeContainer(nodes.Get(1), nodes.Get(2));

    // Create the point-to-point links
    NS_LOG_INFO("Creating channels");
    PointToPointHelper senderRouterLink;
    senderRouterLink.SetDeviceAttribute("DataRate", StringValue("10Mbps")); // High bandwidth
    senderRouterLink.SetChannelAttribute("Delay", StringValue("5ms"));      // Low delay

    PointToPointHelper routerReceiverLink;
    routerReceiverLink.SetDeviceAttribute("DataRate", StringValue("1Mbps")); // Bottleneck link
    routerReceiverLink.SetChannelAttribute("Delay", StringValue("20ms"));    // Higher delay

    // Install devices
    NetDeviceContainer senderRouterDevices = senderRouterLink.Install(senderRouter);
    NetDeviceContainer routerReceiverDevices = routerReceiverLink.Install(routerReceiver);

    // Install Internet stack
    InternetStackHelper internet;
    internet.Install(nodes);

    // Configure queue disciplines
    TrafficControlHelper tch;

    // Set queue limit in packets
    Config::SetDefault(queueDiscType + "::MaxSize",
                       QueueSizeValue(QueueSize(QueueSizeUnit::PACKETS, queueSize)));

    // Configure the bottleneck link queue
    if (queueDiscType == "ns3::RedQueueDisc")
    {
        // Configure RED parameters
        Config::SetDefault("ns3::RedQueueDisc::MinTh", DoubleValue(5));
        Config::SetDefault("ns3::RedQueueDisc::MaxTh", DoubleValue(15));
        Config::SetDefault("ns3::RedQueueDisc::LinkBandwidth", StringValue("1Mbps"));
        Config::SetDefault("ns3::RedQueueDisc::LinkDelay", StringValue("20ms"));
    }
    else if (queueDiscType == "ns3::CoDelQueueDisc")
    {
        // Configure CoDel parameters if needed
        Config::SetDefault("ns3::CoDelQueueDisc::Interval", TimeValue(MilliSeconds(100)));
        Config::SetDefault("ns3::CoDelQueueDisc::Target", TimeValue(MilliSeconds(5)));
    }

    // Install queue disc
    tch.SetRootQueueDisc(queueDiscType);
    tch.Install(routerReceiverDevices.Get(0)); // Install queue disc on router outgoing interface

    // Assign IP Addresses
    NS_LOG_INFO("Assigning IP addresses");
    Ipv4AddressHelper ipv4;

    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer senderRouterIfaces = ipv4.Assign(senderRouterDevices);

    ipv4.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer routerReceiverIfaces = ipv4.Assign(routerReceiverDevices);

    // Set up routing
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Create applications
    NS_LOG_INFO("Creating applications");
    uint16_t port = 50000;

    // Install packet sink on receiver
    PacketSinkHelper sink("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApp = sink.Install(nodes.Get(2));
    sinkApp.Start(Seconds(0.0));
    sinkApp.Stop(Seconds(simulationTime));

    // Install BulkSend on sender
    BulkSendHelper source("ns3::TcpSocketFactory",
                          InetSocketAddress(routerReceiverIfaces.GetAddress(1), port));
    source.SetAttribute("MaxBytes", UintegerValue(maxBytes));
    ApplicationContainer sourceApp = source.Install(nodes.Get(0));
    sourceApp.Start(Seconds(1.0));
    sourceApp.Stop(Seconds(simulationTime - 1));

    // Set up tracing
    if (enableTracing)
    {
        AsciiTraceHelper ascii;
        senderRouterLink.EnableAsciiAll(ascii.CreateFileStream("tcp-ecn-example-sr.tr"));
        routerReceiverLink.EnableAsciiAll(ascii.CreateFileStream("tcp-ecn-example-rr.tr"));
    }

    // Create flow monitor (regardless of tracing setting)
    Ptr<FlowMonitor> flowMonitor;
    FlowMonitorHelper flowHelper;
    flowMonitor = flowHelper.InstallAll();

    if (enableTracing)
    {
        // Schedule flow monitor output
        Simulator::Schedule(Seconds(simulationTime - 0.1),
                            &FlowMonitor::SerializeToXmlFile,
                            flowMonitor,
                            "tcp-ecn-example-flows.xml",
                            true,
                            true);
    }

    // Enable PCAP tracing
    if (enablePcap)
    {
        senderRouterLink.EnablePcapAll("tcp-ecn-example-sr");
        routerReceiverLink.EnablePcapAll("tcp-ecn-example-rr");
    }

    // Run the simulation
    NS_LOG_INFO("Running simulation");
    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();

    // Print results
    Ptr<PacketSink> packetSink = DynamicCast<PacketSink>(sinkApp.Get(0));
    std::cout << "\n=== Simulation Results ===" << std::endl;
    std::cout << "ECN enabled: " << (enableEcn ? "Yes" : "No") << std::endl;
    std::cout << "Queue discipline: " << queueDiscType << std::endl;
    std::cout << "Total bytes received: " << packetSink->GetTotalRx() << " bytes" << std::endl;
    double throughput = (packetSink->GetTotalRx() * 8.0 / (simulationTime - 1) / 1000);
    std::cout << "Average throughput: " << throughput << " Kbps" << std::endl;

    // Print flow monitor statistics
    flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    FlowMonitor::FlowStatsContainer stats = flowMonitor->GetFlowStats();

    std::cout << "\n=== Flow Statistics ===" << std::endl;
    for (auto i = stats.begin(); i != stats.end(); ++i)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);
        std::cout << "Flow " << i->first << " (" << t.sourceAddress << ":" << t.sourcePort << " -> "
                  << t.destinationAddress << ":" << t.destinationPort << ")" << std::endl;
        std::cout << "  Tx Packets: " << i->second.txPackets << std::endl;
        std::cout << "  Rx Packets: " << i->second.rxPackets << std::endl;
        std::cout << "  Lost Packets: " << i->second.lostPackets << std::endl;
        std::cout << "  Packet Loss Ratio: "
                  << (i->second.lostPackets * 100.0 / i->second.txPackets) << "%" << std::endl;
        std::cout << "  Throughput: " << i->second.rxBytes * 8.0 / (simulationTime - 1) / 1000
                  << " Kbps" << std::endl;
        std::cout << "  Mean delay: "
                  << i->second.delaySum.GetSeconds() / i->second.rxPackets * 1000 << " ms"
                  << std::endl;
        std::cout << "  Mean jitter: "
                  << i->second.jitterSum.GetSeconds() / (i->second.rxPackets - 1) * 1000 << " ms"
                  << std::endl;
    }

    std::cout << "\nPCAP files will be available in the working directory if '--enablePcap=true' "
                 "was set.\n"
              << std::endl;

    // Cleanup
    Simulator::Destroy();

    return 0;
}
