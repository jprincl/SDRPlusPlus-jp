#include "LSA_denoise.h"
#include<algorithm>
#include<cmath>

using namespace std;

LSA_denoise::LSA_denoise()
{
}


short LSA_denoise::Initialize(short wlen)
{
	m_lwlen = wlen;
	m_linc13 = m_lwlen/3;
	m_linc23 = m_linc13<<1; // standard frame length
	m_winData = new Complex_num[m_lwlen<<1];
	if (!m_winData) {
		return -15;
	}
	m_ns_hn = new int[m_lwlen];
	if (!m_ns_hn) {
		return -16;
	}
	m_lerr_code=Gc.Initialize(m_linc23); // pass in the true wlen length
	if (m_lerr_code < 0) return m_lerr_code;
	m_lerr_code=MyN_fft.initial(m_linc23);  // 1--14
	if (m_lerr_code <0) return m_lerr_code;  

	for (int i = 1; i < (m_linc23+1); ++i) {   //  2^30= 1073741824
		m_ns_hn[i - 1] = sqrt((0.5 - 0.5 * cos(2.0 * M_PI*(i) / (m_linc23 + 1))) * 1073741824);// 1073741824;
	}

	return 0;
}


short  LSA_denoise::Denoise_process( short* data_in, short* data_out , int blockInd)
{
	if (data_in == NULL)return -17;
	if (data_out == NULL) return -18;
	
	for (int i = 0; i < m_linc23; i++){  // pack the front and back half-frames as two real sequences
		m_winData[i].real = ((__int64)data_in[i] *m_ns_hn[i]) >> 9; // scaled up by 2^6
		m_winData[i].imag = ((__int64)data_in[i + m_linc13] * m_ns_hn[i]) >> 9; // scaled up by 2^6
	} 
 	m_lerr_code=MyN_fft.base4_fft(m_winData, 1);  // fully separates the two packed frames
	if (m_lerr_code < 0) return m_lerr_code;

	m_lerr_code=Gc.G_calculate_process(m_winData, blockInd);  //19--49
    if (m_lerr_code <0) return m_lerr_code;
	m_lerr_code=Gc.G_calculate_process(m_winData + m_linc23, blockInd+1);
	if (m_lerr_code <0) return m_lerr_code;

	m_lerr_code=MyN_fft.base4_fft(m_winData, -1);
    if (m_lerr_code <0) return m_lerr_code;
	m_lerr_code = MyN_fft.base4_fft(m_winData + m_linc23, -1);
	if (m_lerr_code < 0) return m_lerr_code;

	for (int i = 0; i < m_linc23; i++) {
		data_out[i]+= ((__int64)m_winData[i].real * m_ns_hn[i]) >> 21;
		data_out[i+m_linc13] += ((__int64)m_winData[i + m_linc23].real * m_ns_hn[i]) >> 21;
	}

	return 0;
}
LSA_denoise::~LSA_denoise()
{
	if (!m_ns_hn) {
		delete[] m_ns_hn;
		m_ns_hn = NULL;
	}
	if (!m_winData) {
		delete[] m_winData;
		m_winData = NULL;
	}
}
