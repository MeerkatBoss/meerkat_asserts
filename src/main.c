#include <stdio.h>

#include "meerkat_asserts/assert.h"

int foo(int a, int b)
{
  int c = a + b;
  stdassert(c == 4, "2 + 2 is not 4");
  return 0;
}

int bar(int a, int b)
{
  b = 3;
  return foo(a, b);
}

int main()
{
  puts("Hello, asserts!");

  return bar(2, 2);
}
