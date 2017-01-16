#ifndef _CMD_VEL_HPP_
#define _CMD_VEL_HPP_

#include <vector>
#include "sfm.hpp"

namespace sfm
{


class CmdVelProvider
{
public:
	CmdVelProvider(double obstacle_distance_threshold, double robot_max_lin_vel, double robot_max_lin_acc, double robot_max_ang_acc, double beta_v, double beta_y, double beta_d);
	~CmdVelProvider() {}
	void compute(const Agent& robot, double dt);
	const geometry_msgs::Twist& getCommand() const {return command;}

private:
	#define PW(x) ((x)*(x))

	bool checkCollision(const Agent& robot, double linearVelocity, double angularVelocity, double& distance, double& time) const;

	double evaluate(double linVel, double angVel, double distance, double dt,
			const Agent& robot, 
			const utils::Vector2d& velocityRef, const utils::Angle& yawRef);

	double obstacle_distance_threshold;
	double robot_max_lin_vel;
	double robot_max_lin_acc;
	double robot_max_ang_acc;
	double beta_v;
	double beta_y;
	double beta_d;
	std::vector<geometry_msgs::Twist> velocities;
	geometry_msgs::Twist command;


};

inline
CmdVelProvider::CmdVelProvider(double obstacle_distance_threshold, double robot_max_lin_vel,  double robot_max_lin_acc, double robot_max_ang_acc, double beta_v, double beta_y, double beta_d)
: obstacle_distance_threshold(obstacle_distance_threshold),
  robot_max_lin_vel(robot_max_lin_vel),
  robot_max_lin_acc(robot_max_lin_acc),
  robot_max_ang_acc(robot_max_ang_acc),
  beta_v(beta_v),
  beta_y(beta_y),
  beta_d(beta_d)
{
	static const std::vector<double> ang_vels = 
		{-0.8, -0.75, -0.7, -0.65, -0.6, -0.55, -0.5, -0.45, -0.4, 
		-0.35, -0.3, -0.25, -0.2, -0.15, -0.1, -0.05, 0, 0.05, 0.1, 
		0.15, 0.2, 0.25, 0.3, 0.35, 0.4, 0.45, 0.5, 0.55, 0.6, 0.65, 0.7, 0.75, 0.8};
	
	static const std::vector<double> lin_vels = 
		{ 0, 0.05, 0.1, 0.15, 0.2, 0.25, 0.3, 0.35, 0.4, 0.45, 0.5, 0.55, 0.6};

	for (unsigned i = 0; i< lin_vels.size(); i++) {
		for (unsigned j = 0; j<ang_vels.size(); j++) {
			geometry_msgs::Twist twist;
			twist.linear.x = lin_vels[i];
			twist.linear.y = 0;
			twist.linear.z = 0;
			twist.angular.x = 0;
			twist.angular.y = 0;
			twist.angular.z = ang_vels[j];
			velocities.push_back(twist);
		}
	}
};

inline
double CmdVelProvider::evaluate(double linVel, double angVel, double distance, double dt,
			const Agent& robot, 
			const utils::Vector2d& velocityRef, const utils::Angle& yawRef)
{
	if (distance > obstacle_distance_threshold) {
		distance =  obstacle_distance_threshold;
	}
	Agent dummy(robot.position,robot.yaw,linVel,angVel);
	dummy.move(dt);
	double velRef = velocityRef.getX() * robot.yaw.cos() + velocityRef.getY() * robot.yaw.sin();	
	double a = fabs(linVel - velRef)/robot_max_lin_vel;
	utils::Angle angle = dummy.yaw - yawRef;
	double b = fabs(angle.toRadian())/M_PI;
	double c = 1.0 - distance/ obstacle_distance_threshold;
	return beta_v * a + beta_y * b + beta_d *c;
}



inline
bool CmdVelProvider::checkCollision(const Agent& robot, double linearVelocity, double angularVelocity, double& distance, double& time) const
{
	// No linear velocity, no collision
	if (fabs(linearVelocity) < 1e-7) {
		time = 999999;
		distance = 999999;
		return false;
	}
	
	// No angular velocity, special case
	if (fabs(angularVelocity) < 1e-7) {
		for (unsigned k=0;k<2;k++) {
			const std::vector<utils::Vector2d>& obstacles = k==0 ? robot.obstacles1 : robot.obstacles2; 
			for (unsigned i = 0; i< obstacles.size(); i++) {
				if ((linearVelocity>0 && obstacles[i].getX()<0) || (linearVelocity<0 && obstacles[i].getX()>0)) {
					continue;
				}
				if (fabs(obstacles[i].getY())<= robot.radius) {
					distance = fabs(obstacles[i].getX());
					double offset = std::sqrt(PW(robot.radius) - PW(obstacles[i].getY()));
					distance -= offset;
					if (distance <0 ) {
						distance = 0;
					}
					time = distance / fabs(linearVelocity);
					return fabs(linearVelocity) > std::sqrt(2*distance * robot_max_lin_acc);
				}		
			}
		}
		time = 999999;
		distance = 999999;
		return false;
	} 

	// Angular and linear velocity, general case
	utils::Vector2d icc(0,linearVelocity/angularVelocity);
	for (unsigned k = 0; k<2; k++) {
		const std::vector<utils::Vector2d>& obstacles = k==0 ? robot.obstacles1 : robot.obstacles2; 
		for (unsigned i = 0; i< obstacles.size(); i++) {
			utils::Vector2d u = (obstacles[i] - icc).leftNormalVector();
			if (linearVelocity >= 0 && angularVelocity < 0) {
				u.set(-u.getX(),u.getY());
			} else if (linearVelocity <0 && angularVelocity >= 0) {
				u.set(-u.getX(),-u.getY());
			} else if (linearVelocity<0 && angularVelocity<0) {
				u.set(u.getX(),-u.getY());
			}
			utils::Angle alpha = u.angle();
			double t = alpha.toRadian(utils::Angle::PositiveOnlyRange)/fabs(angularVelocity);
			Agent dummy(linearVelocity, angularVelocity);
			dummy.move(t);
			if ((dummy.position - obstacles[i]).squaredNorm() <= PW(robot.radius)) {
				while ((dummy.position - obstacles[i]).squaredNorm() <= PW(robot.radius)) {
					t-=0.01;
					dummy = Agent(linearVelocity, angularVelocity);
					dummy.move(t);
				}
				t+=0.005; 
				time = t;
				if (time <0 ) {
					time = 0;
				}
				distance = fabs(linearVelocity)*time;
				return fabs(linearVelocity) > std::sqrt(2*distance * robot_max_lin_acc) || 
					fabs(angularVelocity) > std::sqrt(2*distance * robot_max_ang_acc);
			}
		}
	}
	time = 999999;
	distance = 999999;
	return false;


}





inline
void CmdVelProvider::compute(const Agent& robot, double dt)
{
	utils::Vector2d velocityRef = robot.velocity + robot.forces.globalForce * dt;
	
	utils::Vector2d positionRef;
	utils::Angle yawRef;
	
	if (velocityRef.norm() > robot_max_lin_vel) {
		velocityRef.normalize();
		velocityRef *= robot_max_lin_vel;
	}
	positionRef = robot.position + velocityRef * dt;
	yawRef = velocityRef.angle();
		
	command.linear.x = 0;
	command.angular.z = 0;
	double min = 999999999;
	double robot_lin_vel = robot.linearVelocity;
	double robot_ang_vel = robot.angularVelocity;

	double distance=0;
	double time = 0;
	ros::Time current_time = ros::Time::now();
	for (unsigned i=0;i<velocities.size();i++) {
		double linVel = velocities[i].linear.x;
		double angVel = velocities[i].angular.z;
		double linAcc = (linVel - robot_lin_vel)/dt;
		double angAcc = (angVel - robot_ang_vel)/dt;
		if (fabs(linAcc) > robot_max_lin_acc ||
			fabs(angAcc) > robot_max_ang_acc) {
			continue;
		}
		if (checkCollision(robot, linVel, angVel,distance,time)) {
			continue;
		}		
		double value = evaluate(linVel, angVel, distance, dt, robot, velocityRef, yawRef);
		if (value < min) {
			min = value;
			command.linear.x = linVel;
			command.angular.z = angVel;
		}
	}
}
}
#endif
