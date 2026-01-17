#ifndef COMMON_UTILS_HH
#define COMMON_UTILS_HH

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define CONTAINER_OF(ptr, type, member) \
    ((type*)((char*)(ptr) - offsetof(type, member)))


#define ASSERT(cond) \
    do { \
        if (!(cond)) { \
            assertImpl(__FILE__, __LINE__); \
        } \
    } while (0) // TODO: Better assert handling

void assertImpl(const char* file, int line);

#endif // COMMON_UTILS_HH