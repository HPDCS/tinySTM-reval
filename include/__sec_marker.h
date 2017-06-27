#ifndef __SEC_MARKER_H
#define __SEC_MARKER_H

#define REGISTER_SEC_MARKER(name) \
	int __attribute__((used)) __##name##_start(void); \
	int __attribute__((used)) __##name##_end(void);

#define MARK_SEC_START(name) \
	int __attribute__((used)) __##name##_start(void) { return 0; }

#define MARK_SEC_END(name) \
	int __attribute__((used)) __##name##_end(void) { return 0; }

#endif
