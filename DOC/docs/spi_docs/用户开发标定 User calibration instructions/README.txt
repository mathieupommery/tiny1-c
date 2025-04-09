用户开发标定【User calibration instructions】
***********************************************************************************************
1.测温与锅盖标定【Secondary calibration& Lid   pattern noise correction】

非制冷红外热像仪对温度敏感，无论模组出厂测温标定如何精确，当用户将模组集成到整机中后，由于热分布的变化、光学结构的变化（加窗口片等），必然引起一定的测温偏差。因此需要在整机端进行测温和锅盖标定。【When the user integrates the module into the whole machine, it will inevitably cause a certain temperature measurement deviation due to the changes of thermal distribution and optical structure (add window, etc.).  Therefore, Secondary calibration& Lid   pattern noise correction is required at the end of the machine. 】

测温标定：测温精度校正【Secondary calibration：Temperature measurement accuracy correction】
锅盖标定：平场矫正【Lid   pattern noise correction：Make full frame output flat】
***************************************************************************************************
2.环境变量校正【Ambient variable correction】

模组测温结果受环反射温度，大气温度，目标发射率，大气透光率，目标距离等因素影响，因此想要获得准确的测温结果，需要对模组进行环境变量矫正。【The temperature measurement of the module is affected by  Emissivity, Atmospheric transmissivity, ambient reflection temperature, ambient atmospheric temperature, target distance, etc. 
】

*************************************************************************************************
3.新增盲元校正【Dead pixel correction】

模组在使用过程中，受到比较严重的机械冲击或静电放电后，有极低的概率出现新增盲元，用户可将新增盲元添加到盲元表中。【If a module is hit by a serious mechanical shock or electrostatic discharge (ESD), there is a very low probability that new dead pixels will appear,. You can add new dead pixels to the dead pixel table.  】
*************************************************************************************************