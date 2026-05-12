#ifndef INTEGRATOR_H
#define INTEGRATOR_H

#include <vector>

class Integrator
{
public:
    std::vector<double> integrate(const std::vector<double>& derivatives, double dt);
};

#endif // INTEGRATOR_H