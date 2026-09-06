#pragma once

#define ESP_RETURN_ON_FALSE(condition, error, tag, format, ...) \
    do {                                                        \
        (void)(tag);                                            \
        (void)(format);                                         \
        if (!(condition)) {                                     \
            return (error);                                     \
        }                                                       \
    } while (0)
