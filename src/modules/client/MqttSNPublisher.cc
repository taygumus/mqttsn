#include "MqttSNPublisher.h"
#include "externals/nlohmann/json.hpp"
#include "helpers/ConversionHelper.h"
#include "helpers/StringHelper.h"
#include "helpers/PacketHelper.h"
#include "helpers/NumericHelper.h"
#include "messages/MqttSNBaseWithWillTopic.h"
#include "messages/MqttSNBaseWithWillMsg.h"
#include "messages/MqttSNBaseWithReturnCode.h"
#include "messages/MqttSNMsgIdWithTopicIdPlus.h"
#include "messages/MqttSNBaseWithMsgId.h"

namespace mqttsn {

Define_Module(MqttSNPublisher);

using json = nlohmann::json;

void MqttSNPublisher::initializeCustom()
{
    willQosFlag = par("willQosFlag");
    willRetainFlag = par("willRetainFlag");
    willTopic = par("willTopic").stringValue();
    willMsg = par("willMsg").stringValue();

    fillTopicsAndData();

    registrationInterval = par("registrationInterval");
    registrationEvent = new inet::ClockEvent("registrationTimer");

    publishInterval = par("publishInterval");
    publishEvent = new inet::ClockEvent("publishTimer");
}

bool MqttSNPublisher::handleMessageWhenUpCustom(omnetpp::cMessage* msg)
{
    if (msg == registrationEvent) {
        handleRegistrationEvent();
    }
    else if (msg == publishEvent) {
        handlePublishEvent();
    }
    else {
        return false;
    }

    return true;
}

void MqttSNPublisher::scheduleActiveStateEventsCustom()
{
    topicIds.clear();
    lastPublish.topicId = 0;
}

void MqttSNPublisher::cancelActiveStateEventsCustom()
{
    cancelEvent(registrationEvent);
    cancelEvent(publishEvent);
}

void MqttSNPublisher::cancelActiveStateClockEventsCustom()
{
    cancelClockEvent(registrationEvent);
    cancelClockEvent(publishEvent);
}

void MqttSNPublisher::processPacketCustom(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort, MsgType msgType)
{
    switch(msgType) {
        // packet types that are allowed only from the selected gateway
        case MsgType::WILLTOPICREQ:
        case MsgType::WILLMSGREQ:
            if (!MqttSNClient::isSelectedGateway(srcAddress, srcPort)) {
                return;
            }
            break;

        // packet types that are allowed only from the connected gateway
        case MsgType::WILLTOPICRESP:
        case MsgType::WILLMSGRESP:
        case MsgType::REGACK:
        case MsgType::PUBACK:
        case MsgType::PUBREC:
        case MsgType::PUBCOMP:
            if (!MqttSNClient::isConnectedGateway(srcAddress, srcPort)) {
                return;
            }
            break;

        default:
            break;
    }

    switch(msgType) {
        case MsgType::WILLTOPICREQ:
            processWillTopicReq(srcAddress, srcPort);
            break;

        case MsgType::WILLMSGREQ:
            processWillMsgReq(srcAddress, srcPort);
            break;

        case MsgType::WILLTOPICRESP:
            processWillResp(pk, true);
            break;

        case MsgType::WILLMSGRESP:
            processWillResp(pk, false);
            break;

        case MsgType::REGACK:
            processRegAck(pk);
            break;

        case MsgType::PUBACK:
            processPubAck(pk);
            break;

        case MsgType::PUBREC:
            processPubRec(pk, srcAddress, srcPort);
            break;

        case MsgType::PUBCOMP:
            processPubComp(pk);
            break;

        default:
            break;
    }
}

void MqttSNPublisher::processConnAckCustom()
{
    scheduleClockEventAfter(registrationInterval, registrationEvent);
    scheduleClockEventAfter(publishInterval, publishEvent);
}

void MqttSNPublisher::processWillTopicReq(const inet::L3Address& srcAddress, const int& srcPort)
{
    sendBaseWithWillTopic(srcAddress, srcPort, MsgType::WILLTOPIC, ConversionHelper::intToQos(willQosFlag), willRetainFlag, willTopic);
}

void MqttSNPublisher::processWillMsgReq(const inet::L3Address& srcAddress, const int& srcPort)
{
    sendBaseWithWillMsg(srcAddress, srcPort, MsgType::WILLMSG, willMsg);
}

void MqttSNPublisher::processWillResp(inet::Packet* pk, bool willTopic)
{
    const auto& payload = pk->peekData<MqttSNBaseWithReturnCode>();

    if (payload->getReturnCode() != ReturnCode::ACCEPTED) {
        return;
    }

    if (willTopic) {
        EV << "Will topic name updated" << std::endl;
        return;
    }

    EV << "Will message updated" << std::endl;
}

void MqttSNPublisher::processRegAck(inet::Packet* pk)
{
    const auto& payload = pk->peekData<MqttSNMsgIdWithTopicIdPlus>();

    // check if the ACK is correct; exit if not
    if (!MqttSNClient::processAckForMsgType(MsgType::REGISTER, payload->getMsgId())) {
        return;
    }

    // now process and analyze message content as needed
    ReturnCode returnCode = payload->getReturnCode();

    if (returnCode == ReturnCode::REJECTED_CONGESTION) {
        lastRegistration.retry = true;
        scheduleClockEventAfter(MqttSNClient::waitingInterval, registrationEvent);
        return;
    }

    if (returnCode == ReturnCode::REJECTED_NOT_SUPPORTED) {
        lastRegistration.retry = false;
        scheduleClockEventAfter(MqttSNClient::waitingInterval, registrationEvent);
        return;
    }

    uint16_t topicId = payload->getTopicId();

    if (returnCode != ReturnCode::ACCEPTED || topicId == 0) {
        throw omnetpp::cRuntimeError("Unexpected error: Invalid return code or topic ID");
    }

    // handle operations when the registration is ACCEPTED; update data structures
    topicIds[topicId] = lastRegistration.info;
    NumericHelper::incrementCounter(&topicsAndData[lastRegistration.info.topicsAndDataKey].counter);

    lastRegistration.retry = false;
    scheduleClockEventAfter(registrationInterval, registrationEvent);
}

void MqttSNPublisher::processPubAck(inet::Packet* pk)
{
    const auto& payload = pk->peekData<MqttSNMsgIdWithTopicIdPlus>();

    if (lastPublish.dataInfo.qosFlag == QoS::QOS_ONE ||
        lastPublish.dataInfo.qosFlag == QoS::QOS_TWO) {

        // check if the ACK is correct; exit if not
        if (!MqttSNClient::processAckForMsgType(MsgType::PUBLISH, payload->getMsgId())) {
            return;
        }
    }

    // now process and analyze message content as needed
    ReturnCode returnCode = payload->getReturnCode();

    if (returnCode == ReturnCode::REJECTED_INVALID_TOPIC_ID) {
        lastRegistration.retry = true;
        lastRegistration.info = lastPublish.registerInfo;

        MqttSNClient::unscheduleMsgRetransmission(MsgType::REGISTER);
        cancelEvent(registrationEvent);

        // retry topic registration
        scheduleClockEventAfter(MqttSNClient::MIN_WAITING_TIME, registrationEvent);

        retryLastPublish();
        return;
    }

    if (returnCode == ReturnCode::REJECTED_CONGESTION) {
        retryLastPublish();
        return;
    }

    if (returnCode != ReturnCode::ACCEPTED) {
        throw omnetpp::cRuntimeError("Unexpected error: Invalid return code");
    }

    // handle operations when publish is ACCEPTED
    lastPublish.retry = false;
    scheduleClockEventAfter(publishInterval, publishEvent);
}

void MqttSNPublisher::processPubRec(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort)
{
    const auto& payload = pk->peekData<MqttSNBaseWithMsgId>();
    uint16_t msgId = payload->getMsgId();

    // check if the ACK is correct; exit if not
    if (!MqttSNClient::processAckForMsgType(MsgType::PUBLISH, msgId)) {
        return;
    }

    // send publish release
    sendBaseWithMsgId(srcAddress, srcPort, MsgType::PUBREL, msgId);

    // schedule publish release retransmission
    MqttSNClient::scheduleRetransmissionWithMsgId(MsgType::PUBREL, msgId);
}

void MqttSNPublisher::processPubComp(inet::Packet* pk)
{
    const auto& payload = pk->peekData<MqttSNBaseWithMsgId>();

    // check if the ACK is correct; exit if not
    if (!MqttSNClient::processAckForMsgType(MsgType::PUBREL, payload->getMsgId())) {
        return;
    }

    // proceed with the next publish
    lastPublish.retry = false;
    scheduleClockEventAfter(publishInterval, publishEvent);
}

void MqttSNPublisher::sendBaseWithWillTopic(const inet::L3Address& destAddress, const int& destPort,
                                            MsgType msgType,
                                            QoS qosFlag, bool retainFlag,
                                            const std::string& willTopic)
{
    const auto& payload = inet::makeShared<MqttSNBaseWithWillTopic>();
    payload->setMsgType(msgType);
    payload->setQoSFlag(qosFlag);
    payload->setRetainFlag(retainFlag);
    payload->setWillTopic(willTopic);
    payload->setChunkLength(inet::B(payload->getLength()));

    std::string packetName;

    switch(msgType) {
        case MsgType::WILLTOPIC:
            packetName = "WillTopicPacket";
            break;

        case MsgType::WILLTOPICUPD:
            packetName = "WillTopicUpdPacket";
            break;

        default:
            packetName = "BaseWithWillTopicPacket";
    }

    inet::Packet* packet = new inet::Packet(packetName.c_str());
    packet->insertAtBack(payload);

    MqttSNApp::socket.sendTo(packet, destAddress, destPort);
}

void MqttSNPublisher::sendBaseWithWillMsg(const inet::L3Address& destAddress, const int& destPort,
                                          MsgType msgType,
                                          const std::string& willMsg)
{
    const auto& payload = inet::makeShared<MqttSNBaseWithWillMsg>();
    payload->setMsgType(msgType);
    payload->setWillMsg(willMsg);
    payload->setChunkLength(inet::B(payload->getLength()));

    std::string packetName;

    switch(msgType) {
        case MsgType::WILLMSG:
            packetName = "WillMsgPacket";
            break;

        case MsgType::WILLMSGUPD:
            packetName = "WillMsgUpdPacket";
            break;

        default:
            packetName = "BaseWithWillMsgPacket";
    }

    inet::Packet* packet = new inet::Packet(packetName.c_str());
    packet->insertAtBack(payload);

    MqttSNApp::socket.sendTo(packet, destAddress, destPort);
}

void MqttSNPublisher::sendRegister(const inet::L3Address& destAddress, const int& destPort,
                                   uint16_t msgId,
                                   const std::string& topicName)
{
    MqttSNApp::socket.sendTo(PacketHelper::getRegisterPacket(msgId, topicName), destAddress, destPort);
}

void MqttSNPublisher::sendPublish(const inet::L3Address& destAddress, const int& destPort,
                                  bool dupFlag, QoS qosFlag, bool retainFlag, TopicIdType topicIdTypeFlag,
                                  uint16_t topicId, uint16_t msgId,
                                  const std::string& data)
{
    MqttSNApp::socket.sendTo(
            PacketHelper::getPublishPacket(dupFlag, qosFlag, retainFlag, topicIdTypeFlag, topicId, msgId, data),
            destAddress,
            destPort
    );
}

void MqttSNPublisher::sendBaseWithMsgId(const inet::L3Address& destAddress, const int& destPort, MsgType msgType, uint16_t msgId)
{
    MqttSNApp::socket.sendTo(PacketHelper::getBaseWithMsgIdPacket(msgType, msgId), destAddress, destPort);
}

void MqttSNPublisher::handleCheckConnectionEventCustom(const inet::L3Address& destAddress, const int& destPort)
{
    MqttSNClient::sendConnect(destAddress, destPort, par("willFlag"), par("cleanSessionFlag"), MqttSNClient::keepAlive);
}

void MqttSNPublisher::handleRegistrationEvent()
{
    std::string topicName;

    // if it's a retry, use the last sent element
    if (lastRegistration.retry) {
        topicName = lastRegistration.info.topicName;
    }
    else {
        // check for topics availability
        if (topicsAndData.empty()) {
            throw omnetpp::cRuntimeError("No topic available");
        }

        // randomly select an element from the map
        auto it = topicsAndData.begin();
        std::advance(it, intuniform(0, topicsAndData.size() - 1));

        topicName = StringHelper::appendCounterToString(it->second.topicName, it->second.counter);

        // update information about the last element
        lastRegistration.info.topicName = topicName;
        lastRegistration.info.topicsAndDataKey = it->first;
        lastRegistration.retry = true;
    }

    sendRegister(MqttSNClient::selectedGateway.address, MqttSNClient::selectedGateway.port, MqttSNClient::getNewMsgId(), topicName);

    // schedule register retransmission
    MqttSNClient::scheduleRetransmissionWithMsgId(MsgType::REGISTER, MqttSNApp::currentMsgId);
}

void MqttSNPublisher::handlePublishEvent()
{
    uint16_t selectedTopicId;
    DataInfo selectedData;

    // if it's a retry, use the last sent element
    if (lastPublish.retry) {
        // update topic ID after publisher reconnection to a server
        if (lastPublish.topicId == 0) {
            findTopicByName(lastPublish.registerInfo.topicName, lastPublish.topicId);
        }

        selectedTopicId = lastPublish.topicId;
        selectedData = lastPublish.dataInfo;
    }
    else {
        // check for topics availability
        if (topicIds.empty()) {
            scheduleClockEventAfter(publishInterval, publishEvent);
            return;
        }

        // randomly select a topic from the map
        auto topicIterator = topicIds.begin();
        std::advance(topicIterator, intuniform(0, topicIds.size() - 1));

        std::map<int, DataInfo> data = topicsAndData[topicIterator->second.topicsAndDataKey].data;

        // check for data availability
        if (data.empty()) {
            scheduleClockEventAfter(publishInterval, publishEvent);
            return;
        }

        // randomly select a data from the map
        auto dataIterator = data.begin();
        std::advance(dataIterator, intuniform(0, data.size() - 1));

        selectedTopicId = topicIterator->first;
        selectedData = dataIterator->second;

        // update information about the last element
        lastPublish.topicId = selectedTopicId;
        lastPublish.registerInfo = topicIterator->second;
        lastPublish.dataInfo = selectedData;

        // retry after publisher reconnection to a server
        if (selectedData.qosFlag != QoS::QOS_ZERO) {
            lastPublish.retry = true;
        }
    }

    QoS qos = selectedData.qosFlag;
    bool retain = selectedData.retainFlag;

    if (qos == QoS::QOS_ZERO) {
        sendPublish(MqttSNClient::selectedGateway.address, MqttSNClient::selectedGateway.port,
                    false, qos, retain, TopicIdType::NORMAL_TOPIC,
                    selectedTopicId, 0,
                    selectedData.message);

        // no need to wait for an ACK
        scheduleClockEventAfter(publishInterval, publishEvent);
        return;
    }

    sendPublish(MqttSNClient::selectedGateway.address, MqttSNClient::selectedGateway.port,
               false, qos, retain, TopicIdType::NORMAL_TOPIC,
               selectedTopicId, MqttSNClient::getNewMsgId(),
               selectedData.message);

    // schedule publish retransmission
    MqttSNClient::scheduleRetransmissionWithMsgId(MsgType::PUBLISH, MqttSNApp::currentMsgId);
}

void MqttSNPublisher::fillTopicsAndData()
{
    json jsonData = json::parse(par("topicsAndDataJson").stringValue());
    int topicsKey = 0;

    // iterate over json object keys (topics) and fill the data structure
    for (auto it = jsonData.begin(); it != jsonData.end(); ++it) {
        TopicAndData topicAndData;
        topicAndData.topicName = it.key();

        int dataKey = 0;

        // iterate over json array elements (messages) and populate the structure
        for (const auto& messageData : it.value()) {
            DataInfo dataInfo;
            dataInfo.qosFlag = ConversionHelper::intToQos(messageData["qos"]);
            dataInfo.retainFlag = messageData["retain"];
            dataInfo.message = messageData["message"];

            topicAndData.data[dataKey++] = dataInfo;
        }

        topicsAndData[topicsKey++] = topicAndData;
    }
}

void MqttSNPublisher::retryLastPublish()
{
    lastPublish.retry = true;

    cancelEvent(publishEvent);
    scheduleClockEventAfter(MqttSNClient::waitingInterval, publishEvent);
}

bool MqttSNPublisher::findTopicByName(const std::string& topicName, uint16_t& topicId)
{
    // iterate through the map to find the specified topic name
    for (const auto& pair : topicIds) {
        if (pair.second.topicName == topicName) {
            // topic name found in the map, assign the corresponding topic ID and return true
            topicId = pair.first;
            return true;
        }
    }

    // topic name not found in the map
    return false;
}

void MqttSNPublisher::handleRetransmissionEventCustom(const inet::L3Address& destAddress, const int& destPort,
                                                      omnetpp::cMessage* msg, MsgType msgType)
{
    switch (msgType) {
        case MsgType::WILLTOPICUPD:
            retransmitWillTopicUpd(destAddress, destPort);
            break;

        case MsgType::WILLMSGUPD:
            retransmitWillMsgUpd(destAddress, destPort);
            break;

        case MsgType::REGISTER:
            retransmitRegister(destAddress, destPort, msg);
            break;

        case MsgType::PUBLISH:
            retransmitPublish(destAddress, destPort, msg);
            break;

        case MsgType::PUBREL:
            retransmitPubRel(destAddress, destPort, msg);
            break;

        default:
            break;
    }
}

void MqttSNPublisher::retransmitWillTopicUpd(const inet::L3Address& destAddress, const int& destPort)
{
    sendBaseWithWillTopic(destAddress, destPort, MsgType::WILLTOPICUPD, ConversionHelper::intToQos(willQosFlag), willRetainFlag, willTopic);
}

void MqttSNPublisher::retransmitWillMsgUpd(const inet::L3Address& destAddress, const int& destPort)
{
    sendBaseWithWillMsg(destAddress, destPort, MsgType::WILLMSGUPD, willMsg);
}

void MqttSNPublisher::retransmitRegister(const inet::L3Address& destAddress, const int& destPort, omnetpp::cMessage* msg)
{
    sendRegister(destAddress, destPort, std::stoi(msg->par("msgId").stringValue()), lastRegistration.info.topicName);
}

void MqttSNPublisher::retransmitPublish(const inet::L3Address& destAddress, const int& destPort, omnetpp::cMessage* msg)
{
    sendPublish(destAddress, destPort,
                true, lastPublish.dataInfo.qosFlag, lastPublish.dataInfo.retainFlag, TopicIdType::NORMAL_TOPIC,
                lastPublish.topicId, std::stoi(msg->par("msgId").stringValue()),
                lastPublish.dataInfo.message);
}

void MqttSNPublisher::retransmitPubRel(const inet::L3Address& destAddress, const int& destPort, omnetpp::cMessage* msg)
{
    sendBaseWithMsgId(destAddress, destPort, MsgType::PUBREL, std::stoi(msg->par("msgId").stringValue()));
}

MqttSNPublisher::~MqttSNPublisher()
{
    cancelAndDelete(registrationEvent);
    cancelAndDelete(publishEvent);
}

} /* namespace mqttsn */
