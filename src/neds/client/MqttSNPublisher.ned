package mqttsn.neds.client;

module MqttSNPublisher extends MqttSNClient
{
    parameters:
        @class(MqttSNPublisher);
        
        bool will = default(false);
        int willQoS = default(0); // valid QoS values: 0, 1, 2 and -1
        bool willRetain = default(false);
        string willTopic = default(""); // will topic name
        string willMsg = default(""); // will message
        
        double registrationInterval @unit(s) = default(5s); // registration interval for new topic names
        int registrationLimit = default(-1); // maximum registrations (predefined topics are excluded), -1 for unlimited
        
        double publishInterval @unit(s) = default(10s); // publish interval for new messages
        int publishLimit = default(-1); // maximum publications, -1 for unlimited
        
        double publishMinusOneInterval @unit(s) = default(20s); // publish interval for new messages with QoS -1
        int publishMinusOneLimit = default(-1); // maximum publications with QoS -1, -1 for unlimited
        string publishMinusOneDestAddress = default(""); // address of the gateway for QoS -1 publications
        int publishMinusOneDestPort = default(-1); // port number of the gateway for QoS -1 publications
}
