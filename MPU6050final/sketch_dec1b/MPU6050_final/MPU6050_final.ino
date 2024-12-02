#include <Wire.h>

// MPU6050 I2C 地址
const uint8_t MPU_ADDR = 0x68;

// 原始加速度数据
int16_t accX, accY, accZ;

// 校准零偏差
float offsetX = 0, offsetY = 0, offsetZ = 0;

// 转换后的加速度数据
float accXg, accYg, accZg;

// 动态阈值设定
const float fastMotionThreshold = 0.6; // 快速运动的加速度阈值
const float slowMotionThreshold = 0.3; // 缓慢移动的阈值
const float returnTolerance = 0.15;    // 归位动作的幅度容差

// 滑动平均滤波器窗口
const int filterSize = 5;
float accXFilter[filterSize] = {0}, accYFilter[filterSize] = {0}, accZFilter[filterSize] = {0};
int filterIndex = 0;

// 未检测到运动的提示间隔
unsigned long lastNoMovementTime = 0;
const unsigned long noMovementInterval = 2000; // 每 2 秒打印一次

// 冷却时间
unsigned long lastMovementTime = 0;
const unsigned long movementCooldown = 3000; // 每 3 秒只输出一次运动结果

// 记录上一次运动的方向和幅度
String lastDirection = "";
float lastMagnitude = 0;

void setup() {
  Serial.begin(115200);
  Wire.begin();

  // 初始化 MPU6050
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B); // Power management register
  Wire.write(0);    // Set to zero (wakes up the MPU-6050)
  Wire.endTransmission(true);

  // 初始校准零偏差
  calibrateOffsets();
}

void loop() {
  // 读取加速度数据
  readAccelData();

  // 滑动平均滤波
  applyFilter();

  // 转换为 g 值
  accXg = (accXFilter[filterIndex] - offsetX) / 16384.0;
  accYg = (accYFilter[filterIndex] - offsetY) / 16384.0;
  accZg = (accZFilter[filterIndex] - offsetZ) / 16384.0;

  // 计算绝对值，确定主要方向
  float absX = fabs(accXg);
  float absY = fabs(accYg);
  float absZ = fabs(accZg);

  String primaryDirection = "";
  float currentMagnitude = 0;
  unsigned long currentTime = millis();

  // 快速运动检测
  if (absX > absY && absX > absZ && absX > fastMotionThreshold) {
    primaryDirection = accXg > 0 ? "X" : "-X";
    currentMagnitude = absX;
  } else if (absY > absX && absY > absZ && absY > fastMotionThreshold) {
    primaryDirection = accYg > 0 ? "Y" : "-Y";
    currentMagnitude = absY;
  } else if (absZ > absX && absZ > absY && absZ > fastMotionThreshold) {
    primaryDirection = accZg > 0 ? "Z" : "-Z";
    currentMagnitude = absZ;
  }

  // 检测归位动作
  if (!primaryDirection.isEmpty() && !lastDirection.isEmpty() && 
      primaryDirection == inverseDirection(lastDirection) &&
      fabs(currentMagnitude - lastMagnitude) < returnTolerance) {
    Serial.println("检测到归位动作，刷新原点");
    calibrateOffsetsFast();
    lastDirection = ""; // 清空上一次运动
    lastMagnitude = 0;
    lastNoMovementTime = currentTime; // 重置无运动计时
  }
  // 输出快速运动结果
  else if (!primaryDirection.isEmpty() && (currentTime - lastMovementTime > movementCooldown)) {
    Serial.println(primaryDirection + " 方向快速运动检测到");
    lastDirection = primaryDirection; // 保存当前运动方向
    lastMagnitude = currentMagnitude; // 保存当前运动幅度
    calibrateOffsetsFast(); // 快速运动后刷新原点
    lastMovementTime = currentTime; // 更新最后一次运动时间
    lastNoMovementTime = currentTime; // 重置无运动计时
  }
  // 检测缓慢移动并刷新原点
  else if (primaryDirection.isEmpty() && 
           (absX > slowMotionThreshold || absY > slowMotionThreshold || absZ > slowMotionThreshold)) {
    calibrateOffsetsFast(); // 缓慢移动时刷新原点
    lastNoMovementTime = currentTime; // 重置无运动计时
  }
  // 未检测到运动
  else if (primaryDirection.isEmpty() && (currentTime - lastNoMovementTime > noMovementInterval)) {
    Serial.println("未检测到运动");
    lastNoMovementTime = currentTime;
  }

  delay(200); // 延迟以便更好地观察数据
}

// 读取加速度数据
void readAccelData() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B); // 加速度数据寄存器的起始地址
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)MPU_ADDR, (size_t)6, (bool)true); // 请求 6 字节数据

  accX = (Wire.read() << 8 | Wire.read());
  accY = (Wire.read() << 8 | Wire.read());
  accZ = (Wire.read() << 8 | Wire.read());
}

// 滑动平均滤波器
void applyFilter() {
  accXFilter[filterIndex] = accX;
  accYFilter[filterIndex] = accY;
  accZFilter[filterIndex] = accZ;

  filterIndex = (filterIndex + 1) % filterSize;

  float sumX = 0, sumY = 0, sumZ = 0;
  for (int i = 0; i < filterSize; i++) {
    sumX += accXFilter[i];
    sumY += accYFilter[i];
    sumZ += accZFilter[i];
  }

  accXFilter[filterIndex] = sumX / filterSize;
  accYFilter[filterIndex] = sumY / filterSize;
  accZFilter[filterIndex] = sumZ / filterSize;
}

// 初始零偏差校准（完整采样）
void calibrateOffsets() {
  const int sampleCount = 100; // 校准采样次数
  long sumX = 0, sumY = 0, sumZ = 0;

  for (int i = 0; i < sampleCount; i++) {
    readAccelData();
    sumX += accX;
    sumY += accY;
    sumZ += accZ;
    delay(10); // 延迟以便稳定读取
  }

  offsetX = sumX / sampleCount;
  offsetY = sumY / sampleCount;
  offsetZ = sumZ / sampleCount;

  Serial.println("校准完成!");
}

// 快速零偏差校准
void calibrateOffsetsFast() {
  readAccelData();
  offsetX = accX;
  offsetY = accY;
  offsetZ = accZ;
  Serial.println("快速校准完成!");
}

// 获取方向的反方向
String inverseDirection(const String& direction) {
  if (direction.startsWith("-")) {
    return direction.substring(1); // 负方向变正方向
  } else {
    return "-" + direction; // 正方向变负方向
  }
}
