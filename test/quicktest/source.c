int main(int argc, char **argv) {
  int x = 2;
  int y = 7;
  if (x == 0) {
    goto cond;
  }
  if (y == 0)
  cond:
    y = 5;
  printf("x: %d\n", x);
  printf("y: %d\n", y);
  return 0;
}
