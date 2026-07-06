#pragma once
#include "G_calculate.h"
#include <utils/flog.h>
#include <algorithm>
using namespace std;

int G_calculate::expintpow_solution(int v_subscript) {
	int vec = 0;
	int g = 0;

	vec = ((__int64)v_subscript * 100 ) >>24;  // / 0.0001;
	vec = vec < 1 ? 1 : vec;

	g= (m_int_value1[vec - 1]);
	return g;
}

int G_calculate::subexp_solution(int v_subscript) {

	int vec = 0;
	int g = 0;

	vec = ((__int64)v_subscript * 100) >> 24;  // / 0.0001;
	vec = vec < 1 ? 1 : vec;
	g = (m_expsub_value1[vec - 1]);
	return g;
}

int G_calculate::Gvalue_solution(int Gh1_subscript,int pp_subscript) {
	int veci = 0,vecj=0;    // j:m_pp  i:m_Gh1
	int g = 0;				
	veci = min(Gh1_subscript * 100 >> 14,6999);
	vecj = max((pp_subscript * 100 >> 14)-1,0);

    int index = vecj* 7000 + veci;
    if (index >= 700000 || index < 0) {
        flog::info("ERROR: index >= 700000 || index < 0, aborting");
        ::abort();
    }
	g = m_G_value[index];
	return g;
}
