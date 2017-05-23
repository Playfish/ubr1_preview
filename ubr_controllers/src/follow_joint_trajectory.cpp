/*********************************************************************
 *  Software License Agreement (BSD License)
 *
 *  Copyright (c) 2013, Unbounded Robotics Inc.
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

/* Author: Michael Ferguson */

#include <pluginlib/class_list_macros.h>
#include <ubr_controllers/follow_joint_trajectory.h>

using angles::shortest_angular_distance;

PLUGINLIB_EXPORT_CLASS(ubr_controllers::FollowJointTrajectoryController, ubr_controllers::Controller)

namespace ubr_controllers
{

bool FollowJointTrajectoryController::init(ros::NodeHandle& nh, ControllerManager* manager)
{
  /* We absolutely need access to the controller manager */
  if (!manager)
  {
    initialized_ = false;
    return false;
  }

  Controller::init(nh, manager);

  /* No initial sampler */
  boost::mutex::scoped_lock lock(sampler_mutex_);
  sampler_.reset();
  preempted_ = false;

  /* Get Joint Names */
  joint_names_.clear();
  XmlRpc::XmlRpcValue names;
  if (!nh.getParam("joints", names))
  {
    ROS_ERROR_STREAM("No joints given for " << nh.getNamespace());
    return false;
  }
  if (names.getType() != XmlRpc::XmlRpcValue::TypeArray)
  {
    ROS_ERROR_STREAM("Joints not in a list for " << nh.getNamespace());
    return false;
  }
  for (int i = 0; i < names.size(); ++i)
  {
    XmlRpc::XmlRpcValue &name_value = names[i];
    if (name_value.getType() != XmlRpc::XmlRpcValue::TypeString)
    {
      ROS_ERROR_STREAM("Not all joint names are strings for " << nh.getNamespace());
      return false;
    }

    joint_names_.push_back(static_cast<std::string>(name_value));
  }

  /* Get parameters */
  nh.param<bool>("stop_with_action", stop_with_action_, false);

  /* Get Joint Handles, setup feedback */
  joints_.clear();
  for (size_t i = 0; i < joint_names_.size(); ++i)
  {
    JointHandle* j = manager_->getJointHandle(joint_names_[i]);
    feedback_.joint_names.push_back(j->getName());
    joints_.push_back(j);
  }

  /* Update feedback */
  feedback_.desired.positions.resize(joints_.size());
  feedback_.desired.velocities.resize(joints_.size());
  feedback_.desired.accelerations.resize(joints_.size());
  feedback_.actual.positions.resize(joints_.size());
  feedback_.actual.velocities.resize(joints_.size());
  feedback_.actual.effort.resize(joints_.size());
  feedback_.error.positions.resize(joints_.size());
  feedback_.error.velocities.resize(joints_.size());

  /* Update tolerances */
  path_tolerance_.q.resize(joints_.size());
  path_tolerance_.qd.resize(joints_.size());
  path_tolerance_.qdd.resize(joints_.size());
  goal_tolerance_.q.resize(joints_.size());
  goal_tolerance_.qd.resize(joints_.size());
  goal_tolerance_.qdd.resize(joints_.size()); 

  /* Setup ROS interfaces */
  server_.reset(new server_t(nh, "",
                             boost::bind(&FollowJointTrajectoryController::executeCb, this, _1),
                             false));
  server_->start();

  initialized_ = true;
  return true;
}

bool FollowJointTrajectoryController::start()
{
  if (!initialized_)
  {
    ROS_ERROR_NAMED("FollowJointTrajectoryController",
                    "Unable to start, not initialized.");
    return false;
  }

  if (!server_->isActive())
  {
    ROS_ERROR_NAMED("FollowJointTrajectoryController",
                    "Unable to start, action server is not active.");
    return false;
  }

  return true;
}

bool FollowJointTrajectoryController::preempt(bool force)
{
  if (!initialized_)
    return true;

  if (server_->isActive())
  {
    if (force)
    {
      /* Shut down the action */
      control_msgs::FollowJointTrajectoryResult result;
      server_->setAborted(result, "Controller manager forced preemption.");
      return true;
    }
    /* Do not abort unless forced */
    return false;
  }

  /* Just holding position, go ahead and preempt us */
  return true;
}

bool FollowJointTrajectoryController::update(const ros::Time now, const ros::Duration dt)
{
  if (!initialized_)
    return false;

  /* Is trajectory active? */
  if (server_->isActive() && sampler_)
  {
    boost::mutex::scoped_lock lock(sampler_mutex_);

    /* Interpolate trajectory */
    TrajectoryPoint p = sampler_->sample(now.toSec());
    last_sample_ = p;

    /* Update joints */
    if (p.q.size() == joints_.size())
    {
      /* Position is good */
      for (size_t i = 0; i < joints_.size(); ++i)
      {
        feedback_.desired.positions[i] = p.q[i];
      }

      if (p.qd.size() == joints_.size())
      {
        /* Velocity is good */
        for (size_t i = 0; i < joints_.size(); ++i)
        {
          feedback_.desired.velocities[i] = p.qd[i];
        }

        if (p.qdd.size() == joints_.size())
        {
          /* Acceleration is good */
          for (size_t i = 0; i < joints_.size(); ++i)
          {
            feedback_.desired.accelerations[i] = p.qdd[i];
          }
        }
      }

      /* Fill in actual */
      for (int j = 0; j < joints_.size(); ++j)
      {
        feedback_.actual.positions[j] = joints_[j]->getPosition();
        feedback_.actual.velocities[j] = joints_[j]->getVelocity();
        feedback_.actual.effort[j] = joints_[j]->getEffort();
      }

      /* Fill in error */
      for (int j = 0; j < joints_.size(); ++j)
      {
        feedback_.error.positions[j] = shortest_angular_distance(feedback_.desired.positions[j],
                                                                 feedback_.actual.positions[j]);
        feedback_.error.velocities[j] = feedback_.actual.velocities[j] -
                                                 feedback_.desired.velocities[j];
      }

      /* Check that we are within path tolerance */
      if (has_path_tolerance_)
      {
        for (size_t j = 0; j < joints_.size(); ++j)
        {
          if ((path_tolerance_.q[j] > 0) &&
              (fabs(feedback_.error.positions[j]) > path_tolerance_.q[j]))
          {
            control_msgs::FollowJointTrajectoryResult result;
            result.error_code = control_msgs::FollowJointTrajectoryResult::PATH_TOLERANCE_VIOLATED;
            server_->setAborted(result, "Trajectory path tolerances violated (position).");
            ROS_ERROR("Trajectory path tolerances violated (position).");
          }

          if ((path_tolerance_.qd[j] > 0) &&
              (fabs(feedback_.error.velocities[j]) > path_tolerance_.qd[j]))
          {
            control_msgs::FollowJointTrajectoryResult result;
            result.error_code = control_msgs::FollowJointTrajectoryResult::PATH_TOLERANCE_VIOLATED;
            server_->setAborted(result, "Trajectory path tolerances violated (velocity).");
            ROS_ERROR("Trajectory path tolerances violated (velocity).");
          }
        }
      }

      /* Check that we are within goal tolerance */
      if (now.toSec() >= sampler_->end_time())
      {
        bool inside_tolerances = true;
        for (size_t j = 0; j < joints_.size(); ++j)
        {
          if ((goal_tolerance_.q[j] > 0) &&
              (fabs(feedback_.error.positions[j]) > goal_tolerance_.q[j]))
          {
            inside_tolerances = false;
          }
        }

        if (inside_tolerances)
        {
          control_msgs::FollowJointTrajectoryResult result;
          result.error_code = control_msgs::FollowJointTrajectoryResult::SUCCESSFUL;
          server_->setSucceeded(result, "Trajectory succeeded.");
          ROS_DEBUG("Trajectory succeeded");
        }
        else if (now.toSec() > (sampler_->end_time() + goal_time_tolerance_ + 0.6))  // 0.6s matches PR2
        {
          control_msgs::FollowJointTrajectoryResult result;
          result.error_code = control_msgs::FollowJointTrajectoryResult::GOAL_TOLERANCE_VIOLATED;
          server_->setAborted(result, "Trajectory not executed within time limits.");
          ROS_ERROR("Trajectory not executed within time limits");
        }
      }

      /* Update joints */
      for (size_t j = 0; j < joints_.size(); ++j)
      {
        joints_[j]->setPositionCommand(feedback_.desired.positions[j],
                                       feedback_.desired.velocities[j],
                                       0.0);
      }
      return true;
    }
  }
  else if (last_sample_.q.size() == joints_.size())
  {
    /* Hold Position */
    for (size_t j = 0; j < joints_.size(); ++j)
    {
      joints_[j]->setPositionCommand(last_sample_.q[j],
                                     0.0,
                                     0.0);
    }
    return true;
  }

  /* No goal and no sample, can't update */
  return false;
}

/*
 * Specification is basically the message:
 * http://ros.org/doc/hydro/api/control_msgs/html/action/FollowJointTrajectory.html
 */
void FollowJointTrajectoryController::executeCb(const control_msgs::FollowJointTrajectoryGoalConstPtr& goal)
{
  control_msgs::FollowJointTrajectoryResult result;

  if (!initialized_)
  {
    server_->setAborted(result, "Controller is not initialized.");
    return;
  }

  if (goal->trajectory.points.empty())
  {
    /* Stop */
    manager_->requestStop(name_);
    return;
  }

  if (goal->trajectory.joint_names.size() != joints_.size())
  {
    result.error_code = control_msgs::FollowJointTrajectoryResult::INVALID_JOINTS;
    server_->setAborted(result, "Trajectory goal size does not match controlled joints size.");
    ROS_ERROR("Trajectory goal size does not match controlled joints size.");
    return;
  }

  Trajectory new_trajectory;
  Trajectory executable_trajectory;

  /* Make a trajectory from our message */
  if (!trajectoryFromMsg(goal->trajectory, joint_names_, &new_trajectory))
  {
    result.error_code = control_msgs::FollowJointTrajectoryResult::INVALID_JOINTS;
    server_->setAborted(result, "Trajectory goal does not match controlled joints");
    ROS_ERROR("Trajectory goal does not match controlled joints");
    return;
  }

  /* If preempted, need to splice on things together */
  if (preempted_)
  {
    /* If the sampler had a large trajectory, we may just be cutting into it */
    if (sampler_ && (sampler_->getTrajectory().size() > 2))
    {
      if (!spliceTrajectories(sampler_->getTrajectory(),
                              new_trajectory,
                              ros::Time::now().toSec(),
                              &executable_trajectory))
      {
        result.error_code = control_msgs::FollowJointTrajectoryResult::INVALID_JOINTS;
        server_->setAborted(result, "Unable to splice trajectory");
        ROS_ERROR("Unable to splice trajectory");
        return;
      }
    }
    else
    {
      /* Previous trajectory was only 2 points, use last_sample + new trajectory */
      Trajectory t;
      t.points.push_back(last_sample_);
      if (!spliceTrajectories(t,
                              new_trajectory,
                              0.0, /* take all points */
                              &executable_trajectory))
      {
        result.error_code = control_msgs::FollowJointTrajectoryResult::INVALID_JOINTS;
        server_->setAborted(result, "Unable to splice trajectory");
        ROS_ERROR("Unable to splice trajectory");
        return;
      }
    }
  }
  else
  {
    if (new_trajectory.size() > 1)
    {
      // use the generated trajectory
      executable_trajectory = new_trajectory;

      // if this hasn't started yet, need to insert current position
      if (goal->trajectory.points[0].time_from_start.toSec() > 0.0)
      {
        executable_trajectory.points.insert(
          executable_trajectory.points.begin(),
          getPointFromCurrent(new_trajectory.points[0].qd.size() > 0,
                              new_trajectory.points[0].qdd.size() > 0,
                              true));
      }
    }
    else
    {
      /* A single point, with nothing in the queue! */
      executable_trajectory.points.push_back(
          getPointFromCurrent(new_trajectory.points[0].qd.size() > 0,
                              new_trajectory.points[0].qdd.size() > 0,
                              false));
      executable_trajectory.points.push_back(new_trajectory.points[0]);
    }
  }

  /* Create trajectory sampler */
  {
    boost::mutex::scoped_lock lock(sampler_mutex_);
    sampler_.reset(new SplineTrajectorySampler(executable_trajectory));
  }

  /* Convert the path tolerances into a more usable form. */
  if (goal->path_tolerance.size() == joints_.size())
  {
    has_path_tolerance_ = true;
    for (size_t j = 0; j < joints_.size(); ++j)
    {
      int index = -1;
      for (size_t i = 0; i < goal->path_tolerance.size(); ++i)
      {
        if (joints_[j]->getName() == goal->path_tolerance[i].name)
        {
          index = i;
          break;
        }
        if (index == -1)
        {
          result.error_code = control_msgs::FollowJointTrajectoryResult::INVALID_JOINTS;
          server_->setAborted(result, "Unable to convert path tolerances");
          ROS_ERROR("Unable to convert path tolerances");
          return;
        }
        path_tolerance_.q[j] = goal->path_tolerance[i].position;
        path_tolerance_.qd[j] = goal->path_tolerance[i].velocity;
        path_tolerance_.qdd[j] = goal->path_tolerance[i].acceleration;
      }
    }
  }
  else
  {
    has_path_tolerance_ = false;
  }

  /* Convert the goal tolerances into a more usable form. */
  if (goal->goal_tolerance.size() == joints_.size())
  {
    for (size_t j = 0; j < joints_.size(); ++j)
    {
      int index = -1;
      for (size_t i = 0; i < goal->goal_tolerance.size(); ++i)
      {
        if (joints_[j]->getName() == goal->goal_tolerance[i].name)
        {
          index = i;
          break;
        }
        if (index == -1)
        {
          result.error_code = control_msgs::FollowJointTrajectoryResult::INVALID_JOINTS;
          server_->setAborted(result, "Unable to convert goal tolerances");
          ROS_ERROR("Unable to convert goal tolerances");
          return;
        }
        goal_tolerance_.q[j] = goal->goal_tolerance[i].position;
        goal_tolerance_.qd[j] = goal->goal_tolerance[i].velocity;
        goal_tolerance_.qdd[j] = goal->goal_tolerance[i].acceleration;
      }
    }
  }
  else
  {
    /* Set defaults */
    for (size_t j = 0; j < joints_.size(); ++j)
    {
      goal_tolerance_.q[j] = 0.02;  // tolerance is same as PR2
      goal_tolerance_.qd[j] = 0.02;
      goal_tolerance_.qdd[j] = 0.02;
    }
  }
  goal_time_tolerance_ = goal->goal_time_tolerance.toSec();

  ROS_DEBUG("Executing new trajectory");

  if (!manager_->requestStart(name_))
  {
    result.error_code = control_msgs::FollowJointTrajectoryResult::GOAL_TOLERANCE_VIOLATED;
    server_->setAborted(result, "Cannot execute trajectory, unable to start controller.");
    ROS_ERROR("Cannot execute trajectory, unable to start controller.");
    return;
  }

  preempted_ = false;
  while (server_->isActive())
  {
    if (server_->isPreemptRequested())
    {
      control_msgs::FollowJointTrajectoryResult result;
      server_->setPreempted(result, "Trajectory preempted");
      ROS_DEBUG("Trajectory preempted");
      preempted_ = true;
      break;
    }

    /* publish feedback */
    feedback_.header.stamp = ros::Time::now();
    server_->publishFeedback(feedback_);
    ros::Duration(1/50.0).sleep();
  }

  {
    boost::mutex::scoped_lock lock(sampler_mutex_);
    sampler_.reset();
  }

  /* Stop this controller if desired (and not preempted) */
  if (stop_with_action_ && !preempted_)
    manager_->requestStop(name_);

  ROS_DEBUG("Done executing trajectory");
}

TrajectoryPoint FollowJointTrajectoryController::getPointFromCurrent(
  bool incl_vel, bool incl_acc, bool zero_vel)
{
  TrajectoryPoint point;

  point.q.resize(joints_.size());
  for (size_t j = 0; j < joints_.size(); ++j)
    point.q[j] = joints_[j]->getPosition();

  if (incl_vel && zero_vel)
  {
    point.qd.resize(joints_.size());
    for (size_t j = 0; j < joints_.size(); ++j)
      point.qd[j] = 0.0;
  }
  else if (incl_vel)
  {
    point.qd.resize(joints_.size());
    for (size_t j = 0; j < joints_.size(); ++j)
      point.qd[j] = joints_[j]->getVelocity();
  }

  if (incl_acc)
  {
    point.qdd.resize(joints_.size());
    /* Currently no good measure of acceleration, assume none. */
    for (size_t j = 0; j < joints_.size(); ++j)
      point.qdd[j] = 0.0;
  }

  point.time = ros::Time::now().toSec();

  return point;
}

std::vector<std::string> FollowJointTrajectoryController::getJointNames()
{
  return joint_names_;
}

}  // namespace ubr_controllers
