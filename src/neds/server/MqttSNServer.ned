package mqttsn.neds.server;

import mqttsn.neds.MqttSNApp;

module MqttSNServer extends MqttSNApp
{
    parameters:
        @class(MqttSNServer);
        
        // time intervals for each state, -1s means forever
        double offlineStateInterval @unit(s) = default(2s);
        double onlineStateInterval @unit(s) = default(20s);
        
        int advertiseInterval @unit(s) = default(900s); // range between 0..65535 seconds (TADV)
        
        double activeClientsCheckInterval @unit(s) = default(500ms); // check interval for verifying active clients
        double asleepClientsCheckInterval @unit(s) = default(500ms); // check interval for verifying asleep clients
        
        int maximumClients = default(10); // maximum clients before congestion
        
        double pendingRetainCheckInterval @unit(s) = default(500ms); // check interval for verifying pending retain messages
        double requestsCheckInterval @unit(s) = default(500ms); // check interval for verifying requests to subscribers
        double registrationsCheckInterval @unit(s) = default(500ms); // check interval for verifying topic registrations
        double awakenSubscriberCheckInterval @unit(s) = default(500ms); // check interval for verifying awaken subscriber
        
        double messagesClearInterval @unit(s) = default(60s); // interval for clearing request messages
}
