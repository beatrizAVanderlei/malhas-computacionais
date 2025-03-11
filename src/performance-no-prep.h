#ifndef PERFORMANCE_NO_PREP_H
#define PERFORMANCE_NO_PREP_H

#include <string>
#include "../models/object/Object.h"

std::vector<int> getVertexFacesNoPrep(const object::Object& obj, int v_index);
std::vector<unsigned int> getVertexAdjacentNoPrep(const object::Object& obj, int v_index);
std::vector<int> getFaceAdjacentNoPrep(const object::Object& obj, int f_index);

double computeMeanNoPrep(const std::vector<double>& values);
double computeStdDevNoPrep(const std::vector<double>& values, double mean);
double computeMeanIntNoPrep(const std::vector<int>& values);

void exportPerformanceDataNoPrep(const object::Object& obj, const std::string &outputFile);

#endif // PERFORMANCE_H
