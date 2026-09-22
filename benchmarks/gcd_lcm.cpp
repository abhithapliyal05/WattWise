// gcd_lcm.cpp — Euclid's algorithm called from a nested loop; mod-heavy.
#include <iostream>
using namespace std;

int gcd(int a, int b) {
    while (b != 0) {
        int t = a % b;
        a = b;
        b = t;
    }
    return a;
}

int main() {
    int coprime = 0;
    int gsum = 0;
    for (int x = 1; x <= 60; x++) {
        for (int y = 1; y <= 60; y++) {
            int g = gcd(x, y);
            gsum += g;
            if (g == 1) coprime++;
        }
    }
    cout << "coprime pairs " << coprime << ", gcd sum " << gsum << endl;
    return 0;
}
