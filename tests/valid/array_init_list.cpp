#include <iostream>
using namespace std;
int main() { int a[5] = {5, 3, 9, 1, 7}; int best = a[0]; for (int i = 1; i < 5; i++) if (a[i] > best) best = a[i]; cout << best << endl; return 0; }
