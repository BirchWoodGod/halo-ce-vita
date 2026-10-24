/* Vita content integration additions by kaboggs. */
#ifndef PC_CONTENT_H
#define PC_CONTENT_H

void pc_content_initialize(void);
void pc_content_new_map(void);
void pc_content_update(void);
/* Scalar-only interface shared by the SDK host and game ABI. */
void pc_content_request(int item);
int pc_content_result(void);

#endif
