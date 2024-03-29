#include "MqttSNBaseWithDuration.h"
#include "types/shared/Length.h"

namespace mqttsn {

MqttSNBaseWithDuration::MqttSNBaseWithDuration()
{
    MqttSNBase::addLength(Length::TWO_OCTETS);
}

void MqttSNBaseWithDuration::setDuration(uint16_t seconds)
{
    duration = seconds;
}

uint16_t MqttSNBaseWithDuration::getDuration() const
{
    return duration;
}

} /* namespace mqttsn */
