// EXPECT: syntax
#include <iostream>
using namespace std;
int sum(int v[], int n) { int s = 0; for (int i = 0; i < n; i++) s += v[i]; return s; }
int main() { int a[4] = {1, 2, 3, 4}; cout << sum(a, 4) << endl; return 0; }
