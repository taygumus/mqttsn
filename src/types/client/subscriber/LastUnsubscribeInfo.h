#ifndef TYPES_CLIENT_SUBSCRIBER_LASTUNSUBSCRIBEINFO_H_
#define TYPES_CLIENT_SUBSCRIBER_LASTUNSUBSCRIBEINFO_H_

struct LastUnsubscribeInfo {
    TopicInfo info;
    bool retry = false;
};

#endif /* TYPES_CLIENT_SUBSCRIBER_LASTUNSUBSCRIBEINFO_H_ */
