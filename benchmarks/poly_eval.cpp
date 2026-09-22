// poly_eval.cpp — floating point: naive vs Horner polynomial evaluation.
// Contains x/2.0 and *2.0 patterns (strength-reduction targets).
#include <iostream>
using namespace std;

double coef[8] = {1.0, -0.5, 0.25, 3.0, -2.0, 0.125, 1.5, -0.75};

double naive(double x) {
    double s = 0.0;
    for (int i = 0; i < 8; i++) {
        double p = 1.0;
        for (int k = 0; k < i; k++) p = p * x;
        s = s + coef[i] * p;
    }
    return s;
}

double horner(double x) {
    double r = 0.0;
    for (int i = 7; i >= 0; i--) r = r * x + coef[i];
    return r;
}

int main() {
    double accN = 0.0;
    double accH = 0.0;
    for (int t = 0; t < 400; t++) {
        double x = t / 2.0 - 100.0;
        double scaled = x * 2.0;
        accN = accN + naive(x / 4.0) + scaled * 0.0;
        accH = accH + horner(x / 4.0);
    }
    cout << accN << " " << accH << endl;
    return 0;
}
