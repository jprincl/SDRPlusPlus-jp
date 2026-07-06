#pragma once
#include"base4_fft.h"
#include"LSA_denoise.h"
// pInBuffer: input staging buffer, allocated by the caller; sized for the largest single read
// pOutBuffer: output staging buffer, allocated by the caller; sized as pInBuffer plus one frame (frame max 4096)
class Datablock_Read{
public:
	Datablock_Read(int sample_rate,int MaxDataLen);
	// returns a status value; if negative an error occurred and the caller should skip denoising and return to its main loop
	short Data_procese(short* pInBuffer, short* pOutBuffer,int read_length, int& out_length);
	~Datablock_Read();

    short m_derr_code;  // error code return value
	// error code ranges: -1..-14 base4_fft, -15..-18 LSA_denoise,
	// -19..-49 G_calculate, -50..-58 Datablock_Read
	int m_maxdata;
	short m_inc, m_wlen,m_blockInd,m_inc_move;
	short m_wlen15;
	int m_sample_rate,m_data_rest_length ;
	short* m_data_in;
	short* m_data_storage;
	int* m_process_storage;
	short* m_data_resize;
	short* m_DoubDataBuffer;
	short* m_data_out;
	LSA_denoise LSA;
	short Initial(int MaxDataLen);

};

