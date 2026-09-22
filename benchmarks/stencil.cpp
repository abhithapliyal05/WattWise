// stencil.cpp — 1-D three-point smoothing, repeated sweeps (double arrays).
// The weight 1.0/3.0 and the index arithmetic are optimisation targets.
#include <iostream>
using namespace std;

double u[512];
double tmp[512];

int main() {
    int n = 512;
    for (int i = 0; i < n; i++) u[i] = (i % 17) * 1.0;
    for (int sweep = 0; sweep < 40; sweep++) {
        for (int i = 1; i < n - 1; i++) {
            double w = 1.0 / 3.0;
            tmp[i] = w * (u[i - 1] + u[i] + u[i + 1]);
        }
        for (int i = 1; i < n - 1; i++) u[i] = tmp[i];
    }
    double s = 0.0;
    for (int i = 0; i < n; i++) s = s + u[i];
    cout << "checksum " << s << endl;
    return 0;
}
