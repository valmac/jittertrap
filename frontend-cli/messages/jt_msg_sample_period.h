#ifndef JT_MSG_SAMPLE_PERIOD_H
#define JT_MSG_SAMPLE_PERIOD_H

int jt_sample_period_unpacker(json_t *root, void **data);
int jt_sample_period_consumer(void *data);


int sample_period;

#endif
