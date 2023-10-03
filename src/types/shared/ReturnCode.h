#ifndef TYPES_RETURNCODE_H_
#define TYPES_RETURNCODE_H_

enum ReturnCode : uint8_t {
    ACCEPTED = 0x00,
    REJECTED_CONGESTION = 0x01,
    REJECTED_INVALID_TOPIC_ID = 0x02,
    REJECTED_NOT_SUPPORTED = 0x03
};

#endif /* TYPES_RETURNCODE_H_ */