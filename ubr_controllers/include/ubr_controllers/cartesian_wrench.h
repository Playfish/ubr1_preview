/*********************************************************************
 *  Software License Agreement (BSD License)
 *
 *  Copyright (c) 2013, Unbounded Robotics Inc.
 *  Copyright (c) 2008, Willow Garage, Inc.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Unbounded Robotics nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************/

/*
 * Derived a bit from pr2_controllers/cartesian_wrench_controller.cpp
 * Author: Michael Ferguson, Wim Meeussen
 */

#ifndef UBR_CONTROLLERS_CARTESIAN_WRENCH_H_
#define UBR_CONTROLLERS_CARTESIAN_WRENCH_H_

#include <string>
#include <vector>
#include <boost/shared_ptr.hpp>

#include <ros/ros.h>
#include <ubr_controllers/controller.h>
#include <ubr_controllers/joint_handle.h>
#include <geometry_msgs/Wrench.h>

#include <kdl/chain.hpp>
#include <kdl/chainjnttojacsolver.hpp>
#include <kdl/frames.hpp>

#include <tf/transform_datatypes.h>
#include <tf/transform_listener.h>

namespace ubr_controllers
{

class CartesianWrenchController : public Controller
{
public:
  CartesianWrenchController() : initialized_(false) {}
  virtual ~CartesianWrenchController() {}

  /**
   *  \brief Initialize parameters, interfaces
   */
  virtual bool init(ros::NodeHandle& nh, ControllerManager* manager);

  /**
   *  \brief Start the controller.
   */
  virtual bool start();

  /**
   *  \brief Is this controller the head of the list?
   */
  virtual bool authoritative()
  {
    // should not run things on top of this
    return true;
  }

  /**
   *  \brief Preempt this controller.
   *  \param force If true, this controller will be stopped regardless
   *         of return value.
   *  \returns true if controller preempted successfully.
   */
  virtual bool preempt(bool force)
  {
    // always preempt?
    return true;
  }

  /**
   *  \brief Update controller
   */
  virtual bool update(const ros::Time now, const ros::Duration dt);

  /**
   *  \brief Get a list of joints this controls.
   */
  virtual std::vector<std::string> getJointNames();

  /** \brief Controller command. */
  void command(const geometry_msgs::Wrench::ConstPtr& goal);

private:
  void updateJoints();

  bool initialized_;
  bool enabled_;
  std::string root_link_;
  ros::Time last_command_;

  KDL::Wrench desired_wrench_;

  KDL::Chain kdl_chain_;
  boost::shared_ptr<KDL::ChainJntToJacSolver> jac_solver_;
  KDL::JntArray jnt_pos_;
  KDL::JntArray jnt_eff_;
  KDL::Jacobian jacobian_;

  ros::Publisher feedback_pub_;
  ros::Subscriber command_sub_;

  tf::TransformListener tf_;
  std::vector<JointHandle*> joints_;
};

}  // namespace ubr_controllers

#endif  // UBR_CONTROLLERS_CARTESIAN_WRENCH_H_
