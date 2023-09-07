/**
 * @file assert.h
 * @author MeerkatBoss (solodovnikov.ia@phystech.edu)
 *
 * @brief
 *
 * @version 0.1
 * @date 2023-09-06
 *
 * @copyright Copyright MeerkatBoss (c) 2023
 */
#ifndef __MEERKAT_ASSERTS_ASSERT_H
#define __MEERKAT_ASSERTS_ASSERT_H

#include <stddef.h>
#include <stdlib.h>

void __assert_print(int fd, const char* file, const char* function, size_t line,
                    const char* condition, const char* message);

#define fassert(fd, condition, message)                                        \
  do                                                                           \
  {                                                                            \
    if (!(condition))                                                          \
    {                                                                          \
      __assert_print(fd, __FILE__, __PRETTY_FUNCTION__, __LINE__, #condition,  \
                     message);                                                 \
      abort();                                                                 \
    }                                                                          \
  } while (0)

#define stdassert(condition, message) fassert(2, condition, message)

#endif /* assert.h */
