// triangular.cpp — pair counting with a triangular inner loop (j from i+1).
// The static estimator approximates the inner trip count as bound/2.
#include <iostream>
using namespace std;

int pts[400];

int main() {
    int n = 400;
    for (int i = 0; i < n; i++) pts[i] = (i * 53) % 997;
    int close = 0;
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            int d = pts[i] - pts[j];
            if (d < 0) d = -d;
            if (d < 10) close++;
        }
    }
    cout << "close pairs " << close << endl;
    return 0;
}
