#include <iostream>
using namespace std;
const int N = 10;
int sq[N];
int main() { for (int i = 0; i < N; i++) sq[i] = i * i; int s = 0; for (int i = 0; i < N; i++) s += sq[i]; cout << s << endl; return 0; }
