#ifndef MODULES_CLIENT_MQTTSNPUBLISHER_H_
#define MODULES_CLIENT_MQTTSNPUBLISHER_H_

#include "MqttSNClient.h"
#include "types/client/TopicsAndData.h"

namespace mqttsn {

class MqttSNPublisher : public MqttSNClient
{
    protected:
        // parameters
        int willQosFlag;
        bool willRetainFlag;
        std::string willTopic;
        std::string willMsg;
        double registrationInterval;

        // active publisher state
        inet::ClockEvent* registrationEvent = nullptr;
        std::map<int, TopicsAndData> topicsAndData;

    protected:
        virtual void initializeCustom() override;
        virtual bool handleMessageWhenUpCustom(omnetpp::cMessage* msg) override;

        // active state management
        virtual void scheduleActiveStateEventsCustom() override;
        virtual void cancelActiveStateEventsCustom() override;
        virtual void cancelActiveStateClockEventsCustom() override;

        // process received packets
        virtual void processPacketCustom(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort, MsgType msgType) override;
        virtual void processConnAckCustom() override;
        virtual void processWillTopicReq(const inet::L3Address& srcAddress, const int& srcPort);
        virtual void processWillMsgReq(const inet::L3Address& srcAddress, const int& srcPort);
        virtual void processWillResp(inet::Packet* pk, bool willTopic);

        // send packets
        virtual void sendBaseWithWillTopic(const inet::L3Address& destAddress, const int& destPort, MsgType msgType, QoS qosFlag, bool retainFlag, std::string willTopic);
        virtual void sendBaseWithWillMsg(const inet::L3Address& destAddress, const int& destPort, MsgType msgType, std::string willMsg);

        // event handlers
        virtual void handleCheckConnectionEventCustom(const inet::L3Address& destAddress, const int& destPort) override;
        virtual void handleRegistrationEvent();

        // other functions
        virtual void fillTopicsAndData();

        // retransmissions management
        virtual void handleRetransmissionEventCustom(const inet::L3Address& destAddress, const int& destPort, omnetpp::cMessage* msg, MsgType msgType) override;

        virtual void retransmitWillTopicUpd(const inet::L3Address& destAddress, const int& destPort);
        virtual void retransmitWillMsgUpd(const inet::L3Address& destAddress, const int& destPort);

    public:
        MqttSNPublisher() {};
        ~MqttSNPublisher();
};

} /* namespace mqttsn */

#endif /* MODULES_CLIENT_MQTTSNPUBLISHER_H_ */
