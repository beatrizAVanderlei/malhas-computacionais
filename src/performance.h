#ifndef PERFORMANCE_H
#define PERFORMANCE_H

#include <string>
#include "../models/object/Object.h"

std::vector<int> getVertexFaces(const object::Object& obj, int v_index);
std::vector<unsigned int> getVertexAdjacent(const object::Object& obj, int v_index);
std::vector<int> getFaceAdjacent(const object::Object& obj, int f_index);

double computeMean(const std::vector<double>& values);
double computeStdDev(const std::vector<double>& values, double mean);
double computeMeanInt(const std::vector<int>& values);

void exportPerformanceData(const object::Object& obj, const std::string &outputFile);

#endif // PERFORMANCE_H
