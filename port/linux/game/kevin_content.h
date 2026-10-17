/* Vita content integration additions by kaboggs. */
#ifndef KEVIN_CONTENT_H
#define KEVIN_CONTENT_H

void kevin_content_initialize(void);
void kevin_content_new_map(void);
void kevin_content_update(void);
/* Scalar-only interface shared by the SDK host and game ABI. */
void kevin_content_request(int item);
int kevin_content_result(void);

#endif
