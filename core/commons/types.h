// core/commons/types.h

#ifndef POWSYS365_TYPES_H
#define POWSYS365_TYPES_H

#include <string>
#include <vector>
#include <complex>

namespace powsys365 {

using Complex = std::complex<double>;
using Vector = std::vector<double>;
using Matrix = std::vector<Vector>;

struct Bus {
    int number;
    std::string name;
    double v_base_kv;
    double vm_pu;
    double va_deg;
    int type; // 1=PQ, 2=PV, 3=Slack
};

struct Line {
    int from_bus;
    int to_bus;
    Complex z_pu;
    double b_pu;
};

struct Generator {
    int bus;
    double p_mw;
    double q_mvar;
    double p_max;
    double p_min;
    double q_max;
    double q_min;
};

struct Load {
    int bus;
    double p_mw;
    double q_mvar;
};

} // namespace powsys365

#endif // POWSYS365_TYPES_H