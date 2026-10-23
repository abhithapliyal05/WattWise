// redundant.cpp — redundant array loads and dead computation in a hot loop.
// Hotspot: the inner body reloads a[i] and b[i] that were already read or just
// stored (redundant-load elimination) and computes a value that is never used
// (dead-code elimination).
#include <iostream>
using namespace std;

int a[512];
int b[512];

int main() {
    int n = 512;
    for (int i = 0; i < n; i++) a[i] = (i * 7 + 3) % 101;
    int total = 0;
    for (int rep = 0; rep < 20; rep++) {
        for (int i = 1; i < n; i++) {
            int x = a[i] + a[i];
            b[i] = x;
            int y = b[i] * 2;
            int dead = a[i - 1] * 5 + y;
            total = total + x + y;
        }
    }
    cout << "total=" << total << endl;
    return 0;
}
