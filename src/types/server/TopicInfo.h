#ifndef TYPES_SERVER_TOPICINFO_H_
#define TYPES_SERVER_TOPICINFO_H_

struct TopicInfo {
    std::string topicName = "";
    TopicIdType topicIdType = TopicIdType::NORMAL_TOPIC_ID;
};

#endif /* TYPES_SERVER_TOPICINFO_H_ */
