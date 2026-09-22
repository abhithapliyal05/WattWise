// prefix_sum.cpp — running sums and a sliding window; repeated array loads.
#include <iostream>
using namespace std;

int v[4096];
int pre[4096];

int main() {
    int n = 4096;
    for (int i = 0; i < n; i++) v[i] = (i * 37 + 11) % 101;
    pre[0] = v[0];
    for (int i = 1; i < n; i++) pre[i] = pre[i - 1] + v[i];
    int w = 64;
    int best = 0;
    for (int i = w; i < n; i++) {
        int s = pre[i] - pre[i - w];
        if (s > best) best = s;
    }
    cout << "total " << pre[n - 1] << " best window " << best << endl;
    return 0;
}
