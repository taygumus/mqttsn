#include "MqttSNServer.h"
#include "inet/networklayer/common/L3AddressResolver.h"
#include "inet/networklayer/common/L3AddressTag_m.h"
#include "inet/transportlayer/common/L4PortTag_m.h"
#include "helpers/StringHelper.h"
#include "helpers/PacketHelper.h"
#include "helpers/NumericHelper.h"
#include "messages/MqttSNAdvertise.h"
#include "messages/MqttSNConnect.h"
#include "messages/MqttSNBase.h"
#include "messages/MqttSNBaseWithReturnCode.h"
#include "messages/MqttSNBaseWithWillTopic.h"
#include "messages/MqttSNBaseWithWillMsg.h"
#include "messages/MqttSNDisconnect.h"
#include "messages/MqttSNPingReq.h"
#include "messages/MqttSNRegister.h"
#include "messages/MqttSNMsgIdWithTopicIdPlus.h"
#include "messages/MqttSNPublish.h"
#include "messages/MqttSNSubscribe.h"
#include "messages/MqttSNSubAck.h"
#include "messages/MqttSNUnsubscribe.h"

namespace mqttsn {

Define_Module(MqttSNServer);

int MqttSNServer::gatewayIdCounter = -1;

void MqttSNServer::initialize(int stage)
{
    ClockUserModuleMixin::initialize(stage);

    if (stage == inet::INITSTAGE_LOCAL) {
        stateChangeEvent = new inet::ClockEvent("stateChangeTimer");
        currentState = GatewayState::OFFLINE;

        advertiseInterval = par("advertiseInterval");
        advertiseEvent = new inet::ClockEvent("advertiseTimer");

        if (gatewayIdCounter < UINT8_MAX) {
            gatewayIdCounter++;
        }
        else {
            throw omnetpp::cRuntimeError("The gateway ID counter has reached its maximum limit");
        }
        gatewayId = gatewayIdCounter;

        activeClientsCheckInterval = par("activeClientsCheckInterval");
        activeClientsCheckEvent = new inet::ClockEvent("activeClientsCheckTimer");

        asleepClientsCheckInterval = par("asleepClientsCheckInterval");
        asleepClientsCheckEvent = new inet::ClockEvent("asleepClientsCheckTimer");

        clientsClearInterval = par("clientsClearInterval");
        clientsClearEvent = new inet::ClockEvent("clientsClearTimer");

        retransmissionInterval = par("retransmissionInterval");

        requestsRetransmissionEvent = new inet::ClockEvent("requestsRetransmissionTimer");
    }
}

void MqttSNServer::handleMessageWhenUp(omnetpp::cMessage* msg)
{
    if (msg == stateChangeEvent) {
        handleStateChangeEvent();
    }
    else if (msg == advertiseEvent) {
        handleAdvertiseEvent();
    }
    else if (msg == activeClientsCheckEvent) {
        handleActiveClientsCheckEvent();
    }
    else if (msg == asleepClientsCheckEvent) {
        handleAsleepClientsCheckEvent();
    }
    else if (msg == clientsClearEvent) {
        handleClientsClearEvent();
    }
    else if (msg == requestsRetransmissionEvent) {
        handleRequestsRetransmissionEvent();
    }
    else {
        MqttSNApp::socket.processMessage(msg);
    }
}

void MqttSNServer::finish()
{
    inet::ApplicationBase::finish();
}

void MqttSNServer::refreshDisplay() const
{
    inet::ApplicationBase::refreshDisplay();
}

void MqttSNServer::handleStartOperation(inet::LifecycleOperation* operation)
{
    MqttSNApp::socket.setOutputGate(gate("socketOut"));
    MqttSNApp::socket.setCallback(this);

    const char* localAddress = par("localAddress");
    MqttSNApp::socket.bind(*localAddress ? inet::L3AddressResolver().resolve(localAddress) : inet::L3Address(), par("localPort"));
    MqttSNApp::socket.setBroadcast(true);

    EV << "Current gateway state: " << getGatewayStateAsString() << std::endl;

    double currentStateInterval = getStateInterval(currentState);
    if (currentStateInterval != -1) {
        scheduleClockEventAt(currentStateInterval, stateChangeEvent);
    }
}

void MqttSNServer::handleStopOperation(inet::LifecycleOperation* operation)
{
    cancelEvent(stateChangeEvent);
    cancelOnlineStateEvents();

    MqttSNApp::socket.close();
}

void MqttSNServer::handleCrashOperation(inet::LifecycleOperation* operation)
{
    cancelClockEvent(stateChangeEvent);
    cancelClockEvent(advertiseEvent);
    cancelClockEvent(activeClientsCheckEvent);
    cancelClockEvent(asleepClientsCheckEvent);
    cancelClockEvent(clientsClearEvent);
    cancelClockEvent(requestsRetransmissionEvent);

    MqttSNApp::socket.destroy();
}

void MqttSNServer::handleStateChangeEvent()
{
    // get the possible next state based on the current state
    GatewayState nextState = (currentState == GatewayState::ONLINE) ? GatewayState::OFFLINE : GatewayState::ONLINE;

    // get the interval for the next state
    double nextStateInterval = getStateInterval(nextState);
    if (nextStateInterval != -1) {
        scheduleClockEventAfter(nextStateInterval, stateChangeEvent);
    }

    // perform state transition and update if successful
    if (performStateTransition(currentState, nextState)) {
        updateCurrentState(nextState);
    }
}

void MqttSNServer::updateCurrentState(GatewayState nextState)
{
    currentState = nextState;
    EV << "Current gateway state: " << getGatewayStateAsString() << std::endl;
}

void MqttSNServer::scheduleOnlineStateEvents()
{
    scheduleClockEventAfter(advertiseInterval, advertiseEvent);
    scheduleClockEventAfter(activeClientsCheckInterval, activeClientsCheckEvent);
    scheduleClockEventAfter(asleepClientsCheckInterval, asleepClientsCheckEvent);
    scheduleClockEventAfter(clientsClearInterval, clientsClearEvent);
    scheduleClockEventAfter(retransmissionInterval, requestsRetransmissionEvent);
}

void MqttSNServer::cancelOnlineStateEvents()
{
    cancelEvent(advertiseEvent);
    cancelEvent(activeClientsCheckEvent);
    cancelEvent(asleepClientsCheckEvent);
    cancelEvent(clientsClearEvent);
    cancelEvent(requestsRetransmissionEvent);
}

bool MqttSNServer::fromOfflineToOnline()
{
    EV << "Offline -> Online" << std::endl;
    scheduleOnlineStateEvents();

    return true;
}

bool MqttSNServer::fromOnlineToOffline()
{
    EV << "Online -> Offline" << std::endl;
    cancelOnlineStateEvents();

    return true;
}

bool MqttSNServer::performStateTransition(GatewayState currentState, GatewayState nextState)
{
    // determine the transition function based on current and next states
    switch (currentState) {
        case GatewayState::OFFLINE:
            if (nextState == GatewayState::ONLINE) {
                return fromOfflineToOnline();
            }
            break;

        case GatewayState::ONLINE:
            if (nextState == GatewayState::OFFLINE) {
                return fromOnlineToOffline();
            }
            break;

        default:
            break;
    }

    return false;
}


double MqttSNServer::getStateInterval(GatewayState currentState)
{
    // returns the interval duration for the given state
    switch (currentState) {
        case GatewayState::OFFLINE:
            return par("offlineStateInterval");

        case GatewayState::ONLINE:
            return par("onlineStateInterval");
    }
}

std::string MqttSNServer::getGatewayStateAsString()
{
    // get current gateway state as a string
    switch (currentState) {
        case GatewayState::OFFLINE:
            return "Offline";

        case GatewayState::ONLINE:
            return "Online";
    }
}

void MqttSNServer::processPacket(inet::Packet* pk)
{
    inet::L3Address srcAddress = pk->getTag<inet::L3AddressInd>()->getSrcAddress();

    if (currentState == GatewayState::OFFLINE || MqttSNApp::isSelfBroadcastAddress(srcAddress)) {
        delete pk;
        return;
    }

    EV << "Server received packet: " << inet::UdpSocket::getReceivedPacketInfo(pk) << std::endl;

    const auto& header = pk->peekData<MqttSNBase>();

    MqttSNApp::checkPacketIntegrity((inet::B) pk->getByteLength(), (inet::B) header->getLength());
    MsgType msgType = header->getMsgType();

    int srcPort = pk->getTag<inet::L4PortInd>()->getSrcPort();

    switch(msgType) {
        // packet types that require an ACTIVE client state
        case MsgType::WILLTOPIC:
        case MsgType::WILLTOPICUPD:
        case MsgType::WILLMSG:
        case MsgType::WILLMSGUPD:
        case MsgType::PINGRESP:
        case MsgType::REGISTER:
        case MsgType::PUBLISH:
        case MsgType::PUBREL:
        case MsgType::SUBSCRIBE:
        case MsgType::UNSUBSCRIBE:
        case MsgType::PUBACK:
        case MsgType::PUBREC:
        case MsgType::PUBCOMP:
            if (!isClientInState(srcAddress, srcPort, ClientState::ACTIVE)) {
                delete pk;
                return;
            }
            break;

        // packet types that require an ACTIVE or ASLEEP client state
        case MsgType::PINGREQ:
        case MsgType::DISCONNECT:
            if (!isClientInState(srcAddress, srcPort, ClientState::ACTIVE) &&
                !isClientInState(srcAddress, srcPort, ClientState::ASLEEP)) {
                delete pk;
                return;
            }
            break;

        default:
            break;
    }

    switch(msgType) {
        case MsgType::SEARCHGW:
            processSearchGw();
            break;

        case MsgType::CONNECT:
            processConnect(pk, srcAddress, srcPort);
            break;

        case MsgType::WILLTOPIC:
            processWillTopic(pk, srcAddress, srcPort);
            break;

        case MsgType::WILLTOPICUPD:
            processWillTopic(pk, srcAddress, srcPort, true);
            break;

        case MsgType::WILLMSG:
            processWillMsg(pk, srcAddress, srcPort);
            break;

        case MsgType::WILLMSGUPD:
            processWillMsg(pk, srcAddress, srcPort, true);
            break;

        case MsgType::PINGREQ:
            processPingReq(pk, srcAddress, srcPort);
            break;

        case MsgType::PINGRESP:
            processPingResp(srcAddress, srcPort);
            break;

        case MsgType::DISCONNECT:
            processDisconnect(pk, srcAddress, srcPort);
            break;

        case MsgType::REGISTER:
            processRegister(pk, srcAddress, srcPort);
            break;

        case MsgType::PUBLISH:
            processPublish(pk, srcAddress, srcPort);
            break;

        case MsgType::PUBREL:
            processPubRel(pk, srcAddress, srcPort);
            break;

        case MsgType::SUBSCRIBE:
            processSubscribe(pk, srcAddress, srcPort);
            break;

        case MsgType::UNSUBSCRIBE:
            processUnsubscribe(pk, srcAddress, srcPort);
            break;

        case MsgType::PUBACK:
            processPubAck(pk, srcAddress, srcPort);
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

    delete pk;
}

void MqttSNServer::processSearchGw()
{
    MqttSNApp::sendGwInfo(gatewayId);
}

void MqttSNServer::processConnect(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort)
{
    const auto& payload = pk->peekData<MqttSNConnect>();

    // prevent client connection when its protocol ID is not supported
    if (payload->getProtocolId() != 0x01) {
        sendBaseWithReturnCode(srcAddress, srcPort, MsgType::CONNACK, ReturnCode::REJECTED_NOT_SUPPORTED);
        return;
    }

    ClientInfo* clientInfo = getClientInfo(srcAddress, srcPort, true);

    // prevent new client connections when the gateway is congested
    if (isGatewayCongested() && clientInfo->isNew) {
        sendBaseWithReturnCode(srcAddress, srcPort, MsgType::CONNACK, ReturnCode::REJECTED_CONGESTION);
        return;
    }

    bool willFlag = payload->getWillFlag();

    // update client information
    clientInfo->isNew = false;
    clientInfo->clientId = payload->getClientId();
    clientInfo->keepAliveDuration = payload->getDuration();
    clientInfo->currentState = ClientState::ACTIVE;
    clientInfo->lastReceivedMsgTime = getClockTime();

    // TO DO -> Quando cleanSession=true vedere se il client � publisher o subscriber nelle mappe. Per il publisher cancellare elementi will per subscriber cancellare sottoscrizioni
    // clientInfo->cleanSessionFlag = payload->getCleanSessionFlag();

    if (willFlag) {
        // update publisher information
        PublisherInfo* publisherInfo = getPublisherInfo(srcAddress, srcPort, true);
        publisherInfo->willFlag = willFlag;

        MqttSNApp::sendBase(srcAddress, srcPort, MsgType::WILLTOPICREQ);
    }
    else {
        sendBaseWithReturnCode(srcAddress, srcPort, MsgType::CONNACK, ReturnCode::ACCEPTED);
    }
}

void MqttSNServer::processWillTopic(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort, bool isDirectUpdate)
{
    setClientLastMsgTime(srcAddress, srcPort);

    const auto& payload = pk->peekData<MqttSNBaseWithWillTopic>();

    // update publisher information
    PublisherInfo* publisherInfo = getPublisherInfo(srcAddress, srcPort, true);
    publisherInfo->willQoS = (QoS) payload->getQoSFlag();
    publisherInfo->willRetain = payload->getRetainFlag();
    publisherInfo->willTopic = payload->getWillTopic();

    if (isDirectUpdate) {
        sendBaseWithReturnCode(srcAddress, srcPort, MsgType::WILLTOPICRESP, ReturnCode::ACCEPTED);
    }
    else {
        MqttSNApp::sendBase(srcAddress, srcPort, MsgType::WILLMSGREQ);
    }
}

void MqttSNServer::processWillMsg(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort, bool isDirectUpdate)
{
    setClientLastMsgTime(srcAddress, srcPort);

    const auto& payload = pk->peekData<MqttSNBaseWithWillMsg>();

    // update publisher information
    PublisherInfo* publisherInfo = getPublisherInfo(srcAddress, srcPort, true);
    publisherInfo->willMsg = payload->getWillMsg();

    if (isDirectUpdate) {
        sendBaseWithReturnCode(srcAddress, srcPort, MsgType::WILLMSGRESP, ReturnCode::ACCEPTED);
    }
    else {
        sendBaseWithReturnCode(srcAddress, srcPort, MsgType::CONNACK, ReturnCode::ACCEPTED);
    }
}

void MqttSNServer::processPingReq(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort)
{
    // update client information
    ClientInfo* clientInfo = getClientInfo(srcAddress, srcPort);
    clientInfo->lastReceivedMsgTime = getClockTime();

    const auto& payload = pk->peekData<MqttSNPingReq>();
    std::string clientId = payload->getClientId();

    if (!clientId.empty()) {
        // check if the client ID matches the expected client ID
        if (clientInfo->clientId != clientId) {
            return;
        }

        clientInfo->currentState = ClientState::AWAKE;

        // TO DO -> send buffered messages to the client
        // TO DO -> this part can be improved e.g. check if there are buffered messages otherwise do not change state

        clientInfo->currentState = ClientState::ASLEEP;
    }

    MqttSNApp::sendBase(srcAddress, srcPort, MsgType::PINGRESP);
}

void MqttSNServer::processPingResp(const inet::L3Address& srcAddress, const int& srcPort)
{
    // update client information
    ClientInfo* clientInfo = getClientInfo(srcAddress, srcPort);
    clientInfo->lastReceivedMsgTime = getClockTime();
    clientInfo->sentPingReq = false;

    EV << "Received ping response from client: " << srcAddress << ":" << srcPort << std::endl;
}

void MqttSNServer::processDisconnect(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort)
{
    // update client information
    ClientInfo* clientInfo = getClientInfo(srcAddress, srcPort);
    clientInfo->lastReceivedMsgTime = getClockTime();

    const auto& payload = pk->peekData<MqttSNDisconnect>();
    uint16_t sleepDuration = payload->getDuration();

    clientInfo->sleepDuration = sleepDuration;
    clientInfo->currentState = (sleepDuration > 0) ? ClientState::ASLEEP : ClientState::DISCONNECTED;

    // ACK with disconnect message
    MqttSNApp::sendDisconnect(srcAddress, srcPort, sleepDuration);

    // TO DO -> not affect existing subscriptions (6.12)
    // TO DO -> manage disconnect with sleep duration field (6.14)
}

void MqttSNServer::processRegister(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort)
{
    setClientLastMsgTime(srcAddress, srcPort);

    const auto& payload = pk->peekData<MqttSNRegister>();
    uint16_t topicId = payload->getTopicId();
    uint16_t msgId = payload->getMsgId();

    // extract and sanitize the topic name from the payload
    std::string topicName = StringHelper::sanitizeSpaces(payload->getTopicName());

    // if the topic name is empty, reject the registration and send REGACK with error code
    if (topicName.empty()) {
        sendMsgIdWithTopicIdPlus(srcAddress, srcPort, MsgType::REGACK, ReturnCode::REJECTED_NOT_SUPPORTED, topicId, msgId);
        return;
    }

    // encode the sanitized topic name to Base64 for consistent key handling
    std::string encodedTopicName = StringHelper::base64Encode(topicName);

    // check if the topic is already registered; if yes, send ACCEPTED response, otherwise register the topic
    auto it = topicsToIds.find(encodedTopicName);
    if (it != topicsToIds.end()) {
        sendMsgIdWithTopicIdPlus(srcAddress, srcPort, MsgType::REGACK, ReturnCode::ACCEPTED, it->second, msgId);
        return;
    }

    // check if the maximum number of topics is reached; if not, set a new available topic ID
    if (!MqttSNApp::setNextAvailableId(topicIds, currentTopicId, false)) {
        sendMsgIdWithTopicIdPlus(srcAddress, srcPort, MsgType::REGACK, ReturnCode::REJECTED_CONGESTION, topicId, msgId);
        return;
    }

    registerNewTopic(encodedTopicName);

    // send REGACK response with the new topic ID and ACCEPTED status
    sendMsgIdWithTopicIdPlus(srcAddress, srcPort, MsgType::REGACK, ReturnCode::ACCEPTED, currentTopicId, msgId);
}

void MqttSNServer::processPublish(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort)
{
    setClientLastMsgTime(srcAddress, srcPort);

    const auto& payload = pk->peekData<MqttSNPublish>();
    uint16_t topicId = payload->getTopicId();
    uint16_t msgId = payload->getMsgId();

    // check if the topic is registered; if not, send a return code
    if (topicIds.find(topicId) == topicIds.end()) {
        sendMsgIdWithTopicIdPlus(srcAddress, srcPort, MsgType::PUBACK, ReturnCode::REJECTED_INVALID_TOPIC_ID, topicId, msgId);
        return;
    }

    // check if the server is congested; if yes, send a return code
    bool isCongested = false; // TO DO -> checkCongestion(); ///
    if (isCongested) {
        sendMsgIdWithTopicIdPlus(srcAddress, srcPort, MsgType::PUBACK, ReturnCode::REJECTED_CONGESTION, topicId, msgId);
        return;
    }

    QoS qosFlag = (QoS) payload->getQoSFlag();
    std::string data = payload->getData();

    MessageInfo messageInfo;
    messageInfo.dup = payload->getDupFlag();
    messageInfo.qos = qosFlag;
    messageInfo.topicId = topicId;
    messageInfo.data = data;

    if (qosFlag == QoS::QOS_ZERO) {
        // handling QoS 0
        dispatchPublishToSubscribers(messageInfo);
        return;
    }

    // message ID check needed for QoS 1 and QoS 2
    if (msgId == 0) {
        sendMsgIdWithTopicIdPlus(srcAddress, srcPort, MsgType::PUBACK, ReturnCode::REJECTED_NOT_SUPPORTED, topicId, msgId);
        return;
    }

    if (qosFlag == QoS::QOS_ONE) {
        // handling QoS 1
        dispatchPublishToSubscribers(messageInfo);
        sendMsgIdWithTopicIdPlus(srcAddress, srcPort, MsgType::PUBACK, ReturnCode::ACCEPTED, topicId, msgId);
        return;
    }

    // handling QoS 2; update publisher information and respond with PUBREC
    PublisherInfo* publisherInfo = getPublisherInfo(srcAddress, srcPort, true);

    DataInfo dataInfo;
    dataInfo.topicId = topicId;
    dataInfo.data = data;

    // save message data for reuse
    publisherInfo->messages[msgId] = dataInfo;

    // send publish received
    sendBaseWithMsgId(srcAddress, srcPort, MsgType::PUBREC, msgId);
}

void MqttSNServer::processPubRel(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort)
{
    setClientLastMsgTime(srcAddress, srcPort);

    // check if the publisher exists for the given key
    auto publisherIt = publishers.find(std::make_pair(srcAddress, srcPort));
    if (publisherIt == publishers.end()) {
        return;
    }

    const auto& payload = pk->peekData<MqttSNBaseWithMsgId>();
    uint16_t msgId = payload->getMsgId();

    // access the messages associated with the publisher
    std::map<uint16_t, DataInfo>& messages = publisherIt->second.messages;

    // check if the message exists for the given message ID
    auto messageIt = messages.find(msgId);
    if (messageIt != messages.end()) {
        // process the original publish message only once; as required for QoS 2 level
        const DataInfo& dataInfo = messageIt->second;

        MessageInfo messageInfo;
        messageInfo.dup = false;
        messageInfo.qos = QoS::QOS_TWO;
        messageInfo.topicId = dataInfo.topicId;
        messageInfo.data = dataInfo.data;

        // handling QoS 2
        dispatchPublishToSubscribers(messageInfo);

        // after processing, delete the message from the map
        messages.erase(messageIt);
    }

    // send publish complete
    sendBaseWithMsgId(srcAddress, srcPort, MsgType::PUBCOMP, msgId);
}

void MqttSNServer::processSubscribe(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort)
{
    setClientLastMsgTime(srcAddress, srcPort);

    const auto& payload = pk->peekData<MqttSNSubscribe>();
    QoS qosFlag = (QoS) payload->getQoSFlag();
    uint16_t msgId = payload->getMsgId();

    // extract and sanitize the topic name from the payload
    std::string topicName = StringHelper::sanitizeSpaces(payload->getTopicName());

    // if the topic name is empty, reject the subscription and send SUBACK with error code
    if (topicName.empty()) {
        sendSubAck(srcAddress, srcPort, qosFlag, ReturnCode::REJECTED_NOT_SUPPORTED, 0, msgId);
        return;
    }

    // encode the sanitized topic name to Base64 for consistent key handling
    std::string encodedTopicName = StringHelper::base64Encode(topicName);

    // check if the topic is already registered; if not, register it
    auto it = topicsToIds.find(encodedTopicName);
    uint16_t topicId;

    if (it == topicsToIds.end()) {
        // check if the maximum number of topics is reached; if not, set a new available topic ID
        if (!MqttSNApp::setNextAvailableId(topicIds, currentTopicId, false)) {
            sendSubAck(srcAddress, srcPort, qosFlag, ReturnCode::REJECTED_CONGESTION, 0, msgId);
            return;
        }

        registerNewTopic(encodedTopicName);
        topicId = currentTopicId;
    }
    else {
        topicId = it->second;
    }

    // check for an existing subscription and delete it if found
    std::pair<uint16_t, QoS> subscriptionKey;
    if (findSubscription(srcAddress, srcPort, topicId, subscriptionKey)) {
        deleteSubscription(srcAddress, srcPort, subscriptionKey);
    }

    // create a new subscription
    insertSubscription(srcAddress, srcPort, topicId, qosFlag);

    // send ACK message with ACCEPTED code
    sendSubAck(srcAddress, srcPort, qosFlag, ReturnCode::ACCEPTED, topicId, msgId);
}

void MqttSNServer::processUnsubscribe(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort)
{
    setClientLastMsgTime(srcAddress, srcPort);

    const auto& payload = pk->peekData<MqttSNUnsubscribe>();

    // extract and sanitize the topic name from the payload
    std::string topicName = StringHelper::sanitizeSpaces(payload->getTopicName());

    if (!topicName.empty()) {
        // check if the topic is present
        auto it = topicsToIds.find(StringHelper::base64Encode(topicName));
        if (it != topicsToIds.end()) {
            // check for an existing subscription and delete it if found
            std::pair<uint16_t, QoS> subscriptionKey;
            if (findSubscription(srcAddress, srcPort, it->second, subscriptionKey)) {
                deleteSubscription(srcAddress, srcPort, subscriptionKey);
            }
        }
    }

    // send ACK message
    sendBaseWithMsgId(srcAddress, srcPort, MsgType::UNSUBACK, payload->getMsgId());
}

void MqttSNServer::processPubAck(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort)
{
    setClientLastMsgTime(srcAddress, srcPort);

    const auto& payload = pk->peekData<MqttSNMsgIdWithTopicIdPlus>();
    uint16_t msgId = payload->getMsgId();

    if (msgId > 0) {
        // check if the ACK is correct; exit if not
        if (!processRequestAck(msgId, MsgType::PUBLISH)) {
            return;
        }
    }

    // now process and analyze message content as needed
    ReturnCode returnCode = payload->getReturnCode();

    if (returnCode == ReturnCode::REJECTED_INVALID_TOPIC_ID) {
        // search and delete the subscriber subscription
        std::pair<uint16_t, QoS> subscriptionKey;
        if (findSubscription(srcAddress, srcPort, payload->getTopicId(), subscriptionKey)) {
            deleteSubscription(srcAddress, srcPort, subscriptionKey);
        }

        return;
    }

    if (returnCode != ReturnCode::ACCEPTED) {
        throw omnetpp::cRuntimeError("Unexpected error: Invalid return code");
    }
}

void MqttSNServer::processPubRec(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort)
{
    const auto& payload = pk->peekData<MqttSNBaseWithMsgId>();
    uint16_t msgId = payload->getMsgId();

    std::map<uint16_t, RequestInfo>::iterator requestIt;
    std::set<uint16_t>::iterator requestIdIt;

    // check if the ACK is valid; exit if not
    if (!isValidRequest(msgId, MsgType::PUBLISH, requestIt, requestIdIt)) {
        return;
    }

    // send publish release
    sendBaseWithMsgId(srcAddress, srcPort, MsgType::PUBREL, msgId);

    // change request message type
    requestIt->second.messageType = MsgType::PUBREL;
}

void MqttSNServer::processPubComp(inet::Packet* pk)
{
    const auto& payload = pk->peekData<MqttSNBaseWithMsgId>();

    // check if the ACK is correct; exit if not
    if (!processRequestAck(payload->getMsgId(), MsgType::PUBREL)) {
        return;
    }
}

void MqttSNServer::sendAdvertise()
{
    const auto& payload = inet::makeShared<MqttSNAdvertise>();
    payload->setMsgType(MsgType::ADVERTISE);
    payload->setGwId(gatewayId);
    payload->setDuration(advertiseInterval);
    payload->setChunkLength(inet::B(payload->getLength()));

    std::ostringstream str;
    str << "AdvertisePacket"<< "-" << numAdvertiseSent;
    inet::Packet* packet = new inet::Packet(str.str().c_str());
    packet->insertAtBack(payload);

    MqttSNApp::socket.sendTo(packet, inet::L3Address(par("broadcastAddress")), par("destPort"));
    numAdvertiseSent++;
}

void MqttSNServer::sendBaseWithReturnCode(const inet::L3Address& destAddress, const int& destPort,
                                          MsgType msgType, ReturnCode returnCode)
{
    const auto& payload = inet::makeShared<MqttSNBaseWithReturnCode>();
    payload->setMsgType(msgType);
    payload->setReturnCode(returnCode);
    payload->setChunkLength(inet::B(payload->getLength()));

    std::string packetName;

    switch(msgType) {
        case MsgType::CONNACK:
            packetName = "ConnAckPacket";
            break;

        case MsgType::WILLTOPICRESP:
            packetName = "WillTopicRespPacket";
            break;

        case MsgType::WILLMSGRESP:
            packetName = "WillMsgRespPacket";
            break;

        default:
            packetName = "BaseWithReturnCodePacket";
    }

    inet::Packet* packet = new inet::Packet(packetName.c_str());
    packet->insertAtBack(payload);

    MqttSNApp::socket.sendTo(packet, destAddress, destPort);
}

void MqttSNServer::sendMsgIdWithTopicIdPlus(const inet::L3Address& destAddress, const int& destPort,
                                            MsgType msgType, ReturnCode returnCode,
                                            uint16_t topicId, uint16_t msgId)
{
    MqttSNApp::socket.sendTo(PacketHelper::getMsgIdWithTopicIdPlusPacket(msgType, returnCode, topicId, msgId),
                             destAddress,
                             destPort);
}

void MqttSNServer::sendBaseWithMsgId(const inet::L3Address& destAddress, const int& destPort, MsgType msgType, uint16_t msgId)
{
    MqttSNApp::socket.sendTo(PacketHelper::getBaseWithMsgIdPacket(msgType, msgId), destAddress, destPort);
}

void MqttSNServer::sendSubAck(const inet::L3Address& destAddress, const int& destPort,
                              QoS qosFlag, ReturnCode returnCode,
                              uint16_t topicId, uint16_t msgId)
{
    const auto& payload = inet::makeShared<MqttSNSubAck>();
    payload->setMsgType(MsgType::SUBACK);
    payload->setQoSFlag(qosFlag);
    payload->setTopicId(topicId);
    payload->setMsgId(msgId);
    payload->setReturnCode(returnCode);
    payload->setChunkLength(inet::B(payload->getLength()));

    inet::Packet* packet = new inet::Packet("SubAckPacket");
    packet->insertAtBack(payload);

    MqttSNApp::socket.sendTo(packet, destAddress, destPort);
}

void MqttSNServer::sendPublish(const inet::L3Address& destAddress, const int& destPort,
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

void MqttSNServer::handleAdvertiseEvent()
{
    sendAdvertise();

    scheduleClockEventAfter(advertiseInterval, advertiseEvent);
}

void MqttSNServer::handleActiveClientsCheckEvent()
{
    inet::clocktime_t currentTime = getClockTime();

    for (auto it = clients.begin(); it != clients.end(); ++it) {
        ClientInfo& clientInfo = it->second;

        // check if the client is ACTIVE and if the elapsed time from last received message is beyond the keep alive duration
        if (clientInfo.currentState == ClientState::ACTIVE &&
            (currentTime - clientInfo.lastReceivedMsgTime) > clientInfo.keepAliveDuration) {

            if (clientInfo.sentPingReq) {
                // change the expired client state and activate the Will feature
                clientInfo.currentState = ClientState::LOST;
                // TO DO -> Will feature activation
            }
            else {
                // send a solicitation ping request to the expired client
                const std::pair<inet::L3Address, int> clientKey = it->first;

                MqttSNApp::sendPingReq(clientKey.first, clientKey.second);
                clientInfo.sentPingReq = true;
            }
        }
    }

    scheduleClockEventAfter(activeClientsCheckInterval, activeClientsCheckEvent);
}

void MqttSNServer::handleAsleepClientsCheckEvent()
{
    inet::clocktime_t currentTime = getClockTime();

    for (auto it = clients.begin(); it != clients.end(); ++it) {
        ClientInfo& clientInfo = it->second;

        // check if the client is ASLEEP and if the elapsed time from last received message is beyond the sleep duration
        if (clientInfo.currentState == ClientState::ASLEEP &&
            (currentTime - clientInfo.lastReceivedMsgTime) > clientInfo.sleepDuration) {

            // change the expired client state and activate the Will feature
            clientInfo.currentState = ClientState::LOST;
            // TO DO -> Will feature activation
            // TO DO -> Buffering of messages to send
        }
    }

    scheduleClockEventAfter(asleepClientsCheckInterval, asleepClientsCheckEvent);
}

void MqttSNServer::handleClientsClearEvent()
{
    inet::clocktime_t currentTime = getClockTime();

    for (auto it = clients.begin(); it != clients.end();) {
        ClientInfo& clientInfo = it->second;

        // check if the client is LOST or DISCONNECTED and if the elapsed time exceeds the threshold
        if ((clientInfo.currentState == ClientState::LOST || clientInfo.currentState == ClientState::DISCONNECTED) &&
            (currentTime - clientInfo.lastReceivedMsgTime) > par("maximumInactivityTime")) {

            it = clients.erase(it);
        }
        else {
            ++it;
        }
    }

    scheduleClockEventAfter(clientsClearInterval, clientsClearEvent);
}

void MqttSNServer::handleRequestsRetransmissionEvent()
{
    // TO DO

    //scheduleClockEventAfter(retransmissionInterval, requestsRetransmissionEvent);
}

void MqttSNServer::registerNewTopic(const std::string& topicName)
{
    // add the new topic in the data structures
    topicsToIds[topicName] = currentTopicId;
    topicIds.insert(currentTopicId);
}

bool MqttSNServer::isGatewayCongested()
{
    // check for gateway congestion based on clients count
    return clients.size() >= (unsigned int) par("maximumClients");
}

PublisherInfo* MqttSNServer::getPublisherInfo(const inet::L3Address& srcAddress, const int& srcPort, bool insertIfNotFound)
{
    // check if the publisher with the specified address and port is present in the data structure
    auto publisherIterator = publishers.find(std::make_pair(srcAddress, srcPort));

    if (publisherIterator != publishers.end()) {
        return &publisherIterator->second;
    }

    if (insertIfNotFound) {
        // insert a new empty publisher
        PublisherInfo newPublisherInfo;
        publishers[std::make_pair(srcAddress, srcPort)] = newPublisherInfo;

        return &publishers[std::make_pair(srcAddress, srcPort)];
    }

    return nullptr;
}

std::set<std::pair<uint16_t, QoS>> MqttSNServer::getSubscriptionKeysByTopicId(uint16_t topicId)
{
    std::set<std::pair<uint16_t, QoS>> keys = {};

    // check if the topic ID key exists in the map
    auto topicIt = topicIdToQoS.find(topicId);
    if (topicIt == topicIdToQoS.end()) {
        // if the topic ID doesn't exist, return an empty set
        return keys;
    }

    const auto& qosSet = topicIt->second;
    // generate complete <topicId, QoS> keys and insert into the set
    for (const auto& qos : qosSet) {
        keys.insert(std::make_pair(topicId, qos));
    }

    return keys;
}

void MqttSNServer::setClientLastMsgTime(const inet::L3Address& srcAddress, const int& srcPort)
{
    ClientInfo* clientInfo = getClientInfo(srcAddress, srcPort);
    clientInfo->lastReceivedMsgTime = getClockTime();
}

bool MqttSNServer::isClientInState(const inet::L3Address& srcAddress, const int& srcPort, ClientState clientState)
{
    // get client information with the specified IP address and port
    ClientInfo* clientInfo = getClientInfo(srcAddress, srcPort);

    // return true if the client is found and its state matches the requested state, otherwise return false
    return (clientInfo != nullptr && clientInfo->currentState == clientState);
}

ClientInfo* MqttSNServer::getClientInfo(const inet::L3Address& srcAddress, const int& srcPort, bool insertIfNotFound)
{
    // check if the client with the specified address and port is present in the data structure
    auto clientIterator = clients.find(std::make_pair(srcAddress, srcPort));

    if (clientIterator != clients.end()) {
        return &clientIterator->second;
    }

    if (insertIfNotFound) {
        // insert a new empty client
        ClientInfo newClientInfo;
        clients[std::make_pair(srcAddress, srcPort)] = newClientInfo;

        return &clients[std::make_pair(srcAddress, srcPort)];
    }

    return nullptr;
}

void MqttSNServer::dispatchPublishToSubscribers(const MessageInfo& message)
{
    // keys with the same topic ID
    std::set<std::pair<uint16_t, QoS>> keys = getSubscriptionKeysByTopicId(message.topicId);

    // iterate for each QoS
    for (const auto& key : keys) {
        // calculate the minimum QoS level between subscription QoS and incoming publish QoS
        QoS resultQoS = NumericHelper::minQoS(key.second, message.qos);

        // check if the subscription exists for the given key
        auto subscriptionIt = subscriptions.find(key);
        if (subscriptionIt != subscriptions.end()) {
            // retrieve subscribers for the current subscription key
            const auto& subscribers = subscriptionIt->second;

            if (!subscribers.empty() && (resultQoS == QoS::QOS_ONE || resultQoS == QoS::QOS_TWO)) {
                // set new available message ID if possible; otherwise, throw an exception
                MqttSNApp::getNewIdentifier(messageIds, currentMessageId,
                                            "Failed to assign a new message ID. All available message IDs are in use");

                // add the new message in the data structures
                messages[currentMessageId] = message;
                messageIds.insert(currentMessageId);
            }

            // iterate for each subscriber
            for (const auto& subscriber : subscribers) {
               // extract subscriber's address and port
               inet::L3Address subscriberAddr = subscriber.first;
               int subcriberPort = subscriber.second;

               if (resultQoS == QoS::QOS_ZERO) {
                   // send a publish message with QoS zero to the subscriber
                   sendPublish(subscriberAddr, subcriberPort,
                               message.dup, resultQoS, false, TopicIdType::NORMAL_TOPIC,
                               message.topicId, 0,
                               message.data);

                   // continue to the next subscriber
                   continue;
               }

               // new pending request
               addNewRequest(subscriberAddr, subcriberPort, MsgType::PUBLISH, currentMessageId, 0);

               // send a publish message with QoS 1 or QoS 2 to the subscriber
               sendPublish(subscriberAddr, subcriberPort,
                           message.dup, resultQoS, false, TopicIdType::NORMAL_TOPIC,
                           message.topicId, currentRequestId,
                           message.data);
            }
        }
    }
}

void MqttSNServer::addNewRequest(const inet::L3Address& subscriberAddress, const int& subscriberPort,
                                 MsgType messageType, uint16_t messagesKey, uint16_t requestId)
{
    if (requestId == 0) {
        // set new available request ID if possible; otherwise, throw an exception
        MqttSNApp::getNewIdentifier(requestIds, currentRequestId,
                                    "Failed to assign a new request ID. All available request IDs are in use");
    }

    RequestInfo requestInfo;
    requestInfo.subscriberAddress = subscriberAddress;
    requestInfo.subscriberPort = subscriberPort;
    requestInfo.messageType = messageType;
    requestInfo.messagesKey = messagesKey;

    uint16_t resultId = requestId == 0 ? currentRequestId : requestId;

    // add the new request in the data structures
    requests[resultId] = requestInfo;
    requestIds.insert(resultId);
}

bool MqttSNServer::isValidRequest(uint16_t requestId, MsgType messageType,
                                  std::map<uint16_t, RequestInfo>::iterator& requestIt, std::set<uint16_t>::iterator& requestIdIt)
{
    // search for the request ID in the map
    requestIt = requests.find(requestId);
    if (requestIt == requests.end()) {
        return false;
    }

    // check if the request message type matches
    if (requestIt->second.messageType != messageType) {
        return false;
    }

    // search for the request ID in the set
    requestIdIt = requestIds.find(requestId);
    if (requestIdIt == requestIds.end()) {
        return false;
    }

    return true;
}

bool MqttSNServer::processRequestAck(uint16_t requestId, MsgType messageType)
{
    std::map<uint16_t, RequestInfo>::iterator requestIt;
    std::set<uint16_t>::iterator requestIdIt;

    // check if the request is valid and retrieve iterators
    if (!isValidRequest(requestId, messageType, requestIt, requestIdIt)) {
        return false;
    }

    // remove the request ID from both structures
    requests.erase(requestIt);
    requestIds.erase(requestIdIt);

    return true;
}

bool MqttSNServer::findSubscription(const inet::L3Address& subscriberAddress, const int& subscriberPort, uint16_t topicId,
                                    std::pair<uint16_t, QoS>& subscriptionKey)
{
    // keys with the same topic ID
    std::set<std::pair<uint16_t, QoS>> keys = getSubscriptionKeysByTopicId(topicId);

    // iterate for each QoS
    for (const auto& key : keys) {
        // check if the subscription exists for the given key
        auto subscriptionIt = subscriptions.find(key);
        if (subscriptionIt != subscriptions.end()) {
            // check if the subscriber is present in the subscription set
            const auto& subscribers = subscriptionIt->second;

            auto subscriberIt = subscribers.find(std::make_pair(subscriberAddress, subscriberPort));
            if (subscriberIt != subscribers.end()) {
                // subscriber found, update the subscription key parameter and return true
                subscriptionKey = key;
                return true;
            }
        }
    }

    // return false if the subscriber has no subscription
    return false;
}

bool MqttSNServer::insertSubscription(const inet::L3Address& subscriberAddress, const int& subscriberPort,
                                      uint16_t topicId, QoS qos)
{
    // create key pairs
    std::pair<uint16_t, QoS> subscriptionKey = std::make_pair(topicId, qos);
    std::pair<inet::L3Address, int> subscriberKey = std::make_pair(subscriberAddress, subscriberPort);

    // check if the subscription key already exists in the map
    auto subscriptionIt = subscriptions.find(subscriptionKey);
    if (subscriptionIt != subscriptions.end()) {
        // if the key exists, access the subscribers
        auto& subscribers = subscriptionIt->second;

        // check if the subscriber already exists for the given address and port
        if (subscribers.find(subscriberKey) != subscribers.end()) {
            // if the subscriber already exists, return false
            return false;
        }

        // insert the new subscriber into the existing subscription
        subscribers.insert(subscriberKey);
    }
    else {
        // create a new subscription and insert the new subscriber
        subscriptions[subscriptionKey].insert(subscriberKey);

        // insert the QoS value associated with the topic ID
        topicIdToQoS[topicId].insert(qos);
    }

    // return true if the insertion is successful
    return true;
}

bool MqttSNServer::deleteSubscription(const inet::L3Address& subscriberAddress, const int& subscriberPort,
                                      const std::pair<uint16_t, QoS>& subscriptionKey)
{
    // search for the subscription key in the map
    auto subscriptionIt = subscriptions.find(subscriptionKey);
    if (subscriptionIt != subscriptions.end()) {
        // if the key is found, access the subscribers
        auto& subscribers = subscriptionIt->second;

        // check if the subscriber is present
        auto subscriberIt = subscribers.find(std::make_pair(subscriberAddress, subscriberPort));
        if (subscriberIt != subscribers.end()) {
            // delete the subscriber from the map
            subscribers.erase(subscriberIt);

            // check if there are no more subscribers for this subscription key
            if (subscribers.empty()) {
                // remove the subscription key from subscriptions
                subscriptions.erase(subscriptionIt);

                // remove the QoS associated with topic ID if there are no more subscribers
                auto qosSetIt = topicIdToQoS.find(subscriptionKey.first);
                if (qosSetIt != topicIdToQoS.end()) {
                    auto& qosSet = qosSetIt->second;
                    qosSet.erase(subscriptionKey.second);

                    // remove the topic ID if no more QoS are associated
                    if (qosSet.empty()) {
                        topicIdToQoS.erase(qosSetIt);
                    }
                }
            }

            // delete operation is successful
            return true;
        }
    }

    // delete operation is failed
    return false;
}

MqttSNServer::~MqttSNServer()
{
    cancelAndDelete(stateChangeEvent);
    cancelAndDelete(advertiseEvent);
    cancelAndDelete(activeClientsCheckEvent);
    cancelAndDelete(asleepClientsCheckEvent);
    cancelAndDelete(clientsClearEvent);
    cancelAndDelete(requestsRetransmissionEvent);
}

} /* namespace mqttsn */
