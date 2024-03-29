#include "MqttSNBaseWithMsgId.h"
#include "types/shared/Length.h"

namespace mqttsn {

MqttSNBaseWithMsgId::MqttSNBaseWithMsgId()
{
    MqttSNBase::addLength(Length::TWO_OCTETS);
}

void MqttSNBaseWithMsgId::setMsgId(uint16_t messageId)
{
    msgId = messageId;
}

uint16_t MqttSNBaseWithMsgId::getMsgId() const
{
    return msgId;
}

} /* namespace mqttsn */
