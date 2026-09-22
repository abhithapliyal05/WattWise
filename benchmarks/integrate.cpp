// integrate.cpp — trapezoid rule; a function call inside the hot loop.
#include <iostream>
using namespace std;

double f(double x) { return x * x * x - 2.0 * x + 1.0; }

int main() {
    int steps = 20000;
    double a = 0.0;
    double b = 3.0;
    double h = (b - a) / steps;
    double sum = (f(a) + f(b)) / 2.0;
    for (int i = 1; i < steps; i++) {
        sum = sum + f(a + i * h);
    }
    cout << "integral ~ " << sum * h << endl;
    return 0;
}
