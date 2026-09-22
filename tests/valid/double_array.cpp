#include <iostream>
using namespace std;
int main() { double w[4] = {0.5, 1.5, 2.5, 3.5}; double s = 0.0; for (int i = 0; i < 4; i++) s += w[i] * w[i]; cout << s << endl; return 0; }
