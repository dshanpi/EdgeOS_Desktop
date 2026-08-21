#ifndef _SCOPEDTIMING_H
#define _SCOPEDTIMING_H

#include <chrono>
#include <string>
#include <iostream>

/**
 * @brief 计时类
 * 统计在该类实例生命周期内的耗时
 */
class ScopedTiming
{
public:
	/**
	 * @brief ScopedTiming构造函数,初始化计时对象名称并开始计时
	 * @param info 			 计时对象名称
	 * @param enable_profile 是否开始计时
	 * @return None
	 */
	ScopedTiming(std::string info = "ScopedTiming", int enable_profile = 1)
		: m_info(info), enable_profile(enable_profile)
	{
		if (enable_profile)
		{
			m_start = std::chrono::steady_clock::now();
		}
	}

	/**
	 * @brief ScopedTiming析构,结束计时，并打印耗时
	 * @return None
	 */
	~ScopedTiming()
	{
		if (enable_profile)
		{
			m_stop = std::chrono::steady_clock::now();
			double elapsed_ms = std::chrono::duration<double, std::milli>(m_stop - m_start).count();
			std::cout << m_info << " took " << elapsed_ms << " ms" << std::endl;
		}
	}

private:
	int enable_profile;							   // 是否统计时间
	std::string m_info;							   // 计时对象名称
	std::chrono::steady_clock::time_point m_start; // 计时开始时间
	std::chrono::steady_clock::time_point m_stop;  // 计时结束时间
};
#endif