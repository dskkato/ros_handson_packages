// Copyright (c) 2023 OUXT Polaris
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "braitenberg_vehicle/braitenberg_vehicle_controller.hpp"

#include <limits>

namespace braitenberg_vehicle
{

BraitenbergVehicleController::BraitenbergVehicleController(const rclcpp::NodeOptions & options)
: rclcpp::Node("braitenberg_vehicle_controller", options),
  // "base_link_frame_id"パラメータを読み込み、メンバ変数にセット
  base_link_frame_id_(get_parameter("base_link_frame_id", std::string("base_link"))),
  //　パラメータから仮想光センサの取り付け位置を読み込み
  virtual_light_sensor_position_x_offset_(
    get_parameter("virtual_light_sensor_position_x_offset", 0.1)),
  virtual_light_sensor_position_y_offset_(
    get_parameter("virtual_light_sensor_position_y_offset", 0.1)),
  motion_model_(
    // "wheel_radius"パラメータを読み込みメンバ変数にセット、デフォルト値はTurtlebot3 burgerの.xacroファイルより計算
    get_parameter("wheel_radius", 0.033),
    // "wheel_base"パラメータを読み込みメンバ変数二セット、デフォルト値はTurtlebot3 burgerの.xacroファイルより計算
    get_parameter("wheel_base", 0.16))
{
  // publisherを作る関数、テンプレート引数はメッセージ型、第一引数はtopic名、第二引数は送信バッファサイズ
  twist_pub_ = create_publisher<geometry_msgs::msg::Twist>("/cmd_vel", 1);
  // subscriptionを作る関数、テンプレート引数はメッセージ型、第一引数はtopic名、第二引数は受信バッファサイズ、第三引数にコールバック関数を登録
  scan_sub_ = create_subscription<sensor_msgs::msg::LaserScan>(
    "/scan", 1, [this](const sensor_msgs::msg::LaserScan::SharedPtr scan) { scan_callback(scan); });
  // subscriptionを作る関数、テンプレート引数はメッセージ型、第一引数はtopic名、第二引数は受信バッファサイズ、第三引数にコールバック関数を登録
  goal_pose_sub_ = create_subscription<geometry_msgs::msg::PoseStamped>(
    "/goal_pose", 1,
    [this](const geometry_msgs::msg::PoseStamped::SharedPtr pose) { goal_pose_callback(pose); });
}

BraitenbergVehicleController::~BraitenbergVehicleController() {}

void BraitenbergVehicleController::scan_callback(const sensor_msgs::msg::LaserScan::SharedPtr scan)
{
  mutex_.lock();
  mutex_.unlock();
}

void BraitenbergVehicleController::goal_pose_callback(
  const geometry_msgs::msg::PoseStamped::SharedPtr pose)
{
  mutex_.lock();
  // 受信したゴール姿勢のframe_id(座標系の名前)が適切なものかを確認
  if (pose->header.frame_id == base_link_frame_id_) {
    // 適切な場合は、goal_pose_に受信したposeを代入
    goal_pose_ = pose->pose;
  } else {
    // 不適切な場合は、goal_pose_に無効値を表現するstd::nulloptをセット
    goal_pose_ = std::nullopt;
  }
  mutex_.unlock();
}

void BraitenbergVehicleController::timer_callback()
{
  mutex_.lock();
  if (goal_pose_) {
    twist_pub_->publish(motion_model_.get_twist(
      // 左側の車輪には右側の仮想光センサの出力を入力
      // ROSの座標系は前方がX軸、左がY軸、上がZ軸の正方向
      emulate_light_sensor(
        virtual_light_sensor_position_x_offset_, virtual_light_sensor_position_y_offset_ * -1),
      // 右側の車輪には左側の仮想光センサの出力を入力
      // ROSの座標系は前方がX軸、左がY軸、上がZ軸の正方向
      emulate_light_sensor(
        virtual_light_sensor_position_x_offset_, virtual_light_sensor_position_y_offset_)));
  } else {
    twist_pub_->publish(geometry_msgs::msg::Twist());
  }
  mutex_.unlock();
}

// ゴール地点を光源として扱うための仮想光センサ入力を計算するための関数
double BraitenbergVehicleController::emulate_light_sensor(double x_offset, double y_offset) const
{
  // constexprをつけることで変数eはコンパイル時に計算され定数となるので実行時の計算コストを減らすことができる（https://cpprefjp.github.io/lang/cpp11/constexpr.html）
  // std::numeric_limits<double>::epsilon();はdouble型の計算機イプシロン（処理系が取り扱える浮動小数点の誤差幅）である（https://cpprefjp.github.io/reference/limits/numeric_limits/epsilon.html）
  // 浮動小数点型に対して一致を計算する場合には「==演算子」を用いてはならない、計算機イプシロンを考慮して計算する必要がある
  // 例えば、float a = 3.0;したときにa=3であることを確認するには以下のように記述する必要がある
  // constexpr auto e = std::numeric_limits<float>::epsilon();
  // float a = 3.0;
  // if(std::abs(a - 3.0) <= e) { /* a = 3.0のときの処理 */ }
  constexpr auto e = std::numeric_limits<double>::epsilon();
  // 有効なゴール指定がされているかどうかを判定
  if (goal_pose_) {
    // 有効なゴール姿勢がある場合
    // (goal_pose_->position.x - x_offset)^2 + (goal_pose_->position.y - y_offset)^2の平方根を計算して二点間の距離を求める
    if (const auto distance =
          std::hypot(goal_pose_->position.x - x_offset, goal_pose_->position.y - y_offset);
        // 距離の絶対値が計算機イプシロンより小さい、つまりdistance = 0.0の場合
        std::abs(distance) <= e)
      // ゼロ割を回避しつつセンサの出力値を最大にしたいので1を返す
      return 1;
    else {
      // センサの出力は距離に反比例する。
      return 1 / distance;
      ;
    }
  } else {
    // 有効なゴール姿勢がない場合、その場で停止するコマンドを発行するために光源センサの出力を0にする。
    return 0;
  }
}
}  // namespace braitenberg_vehicle
