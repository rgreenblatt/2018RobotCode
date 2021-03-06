#include <elevator_controller/linear_control.h>
#include <ros/console.h>

namespace elevator_controller
{
ElevatorController::ElevatorController():
	after_shift_max_accel_(0),
	after_shift_max_vel_(0),
	before_shift_max_accel_(0),
	before_shift_max_vel_(0),
	intake_up_last_(true),
	transition_time_(0.0),
	shift_cmd_(false),
	shifted_(false),
	clamp_cmd_(0.0),
	climb_height_(0.0),
	end_game_deploy_cmd_(false),
	end_game_deploy_wings_cmd_(false),
	end_game_deploy_t1_(false),
	end_game_deploy_t2_(false),
	end_game_deploy_t3_(false),
	end_game_deploy_start_(0.0),
	end_game_deploy_t2_time_(0.0),
	max_extension_(0.0),
	min_extension_(0.0),
	intake_down_time_(0.0),
	hook_depth_(0.0),
	hook_min_height_(0.0),
	hook_max_height_(0.0),
	arm_length_(0.0),
	pivot_offset_(0.0),
	lift_offset_(0.0),
	intake_power_diff_multiplier_(1.0),
	f_arm_fric_(0.0)
{
}

bool ElevatorController::getFirstString(XmlRpc::XmlRpcValue value, std::string &str)
{
	if (value.getType() == XmlRpc::XmlRpcValue::TypeArray)
	{
		if (value.size() == 0)
		{
			ROS_ERROR_STREAM_NAMED(name_,
					"Joint param is an empty list");
			return false;
		}

		if (value[0].getType() != XmlRpc::XmlRpcValue::TypeString)
		{
			ROS_ERROR_STREAM_NAMED(name_,
					"Joint param #0 isn't a string.");
			return false;
		}
		str = static_cast<std::string>(value[0]);
	}
	else if (value.getType() == XmlRpc::XmlRpcValue::TypeString)
	{
		str = static_cast<std::string>(value);
	}
	else
	{
		ROS_ERROR_STREAM_NAMED(name_,
				"Joint param is neither a list of strings nor a string.");
		return false;
	}

	return true;
}

bool ElevatorController::init(hardware_interface::RobotHW *hw,
                			  ros::NodeHandle &/*root_nh*/,
             	              ros::NodeHandle &controller_nh)
{
	hardware_interface::TalonCommandInterface *const talon_command_iface = hw->get<hardware_interface::TalonCommandInterface>();
	hardware_interface::JointStateInterface *const joint_state_iface = hw->get<hardware_interface::JointStateInterface>();
	hardware_interface::PositionJointInterface *const pos_joint_iface = hw->get<hardware_interface::PositionJointInterface>();

	const std::string complete_ns = controller_nh.getNamespace();
	std::size_t id = complete_ns.find_last_of("/");
	name_ = complete_ns.substr(id + 1);

	// TODO : make these names params?
	line_break_intake_high_ = joint_state_iface->getHandle("intake_line_break_high");
	line_break_intake_low_ = joint_state_iface->getHandle("intake_line_break_low");

	XmlRpc::XmlRpcValue lift_params;
	if (!controller_nh.getParam("lift", lift_params))
	{
		ROS_ERROR_NAMED(name_, "Can not read lift name(s)");
		return false;
	}
	XmlRpc::XmlRpcValue intake1_params;
	if (!controller_nh.getParam("intake1", intake1_params))
	{
		ROS_ERROR_NAMED(name_, "Can not read intake1 name(s)");
		return false;
	}
	XmlRpc::XmlRpcValue intake2_params;
	if (!controller_nh.getParam("intake2", intake2_params))
	{
		ROS_ERROR_NAMED(name_, "Can not read intake2 name(s)");
		return false;
	}
	XmlRpc::XmlRpcValue pivot_params;
	if (!controller_nh.getParam("pivot", pivot_params))
	{
		ROS_ERROR_NAMED(name_, "Can not read pivot name(s)");
		return false;
	}

	if (!controller_nh.getParam("arm_length", arm_length_))
	{
		ROS_ERROR_NAMED(name_, "Can not read arm_length");
		return false;
	}
	if (!controller_nh.getParam("climb_height", climb_height_))
	{
		ROS_ERROR_NAMED(name_, "Can not read climb_height");
		return false;
	}

	if (!controller_nh.getParam("intake_power_diff_multiplier", intake_power_diff_multiplier_))
	{
		ROS_ERROR_NAMED(name_, "Can not read intake_power_diff_multiplier");
		return false;
	}

	if (!controller_nh.getParam("norm_cur_lim", norm_cur_lim_))
	{
		ROS_ERROR_NAMED(name_, "Can not read norm_cur_lim");
		return false;
	}
	if (!controller_nh.getParam("climb_cur_lim", climb_cur_lim_))
	{
		ROS_ERROR_NAMED(name_, "Can not read climb_cur_lim");
		return false;
	}
	if (!controller_nh.getParam("limit_power_press", limit_power_press_))
	{
		ROS_ERROR_NAMED(name_, "Can not read limit_power_press");
		return false;
	}

	std::string node_name;
	//Offset for lift should be lift sensor pos when all the way down + height of carriage pivot point
	//when all the way down
	lift_offset_ = 0;
	if (getFirstString(lift_params, node_name) &&
	   !ros::NodeHandle(controller_nh, node_name).getParam("offset", lift_offset_))
	{
		ROS_ERROR_STREAM("Can not read offset for lift " << node_name);
		return false;
	}

	//Offset for arm should be the angle at arm all the way up, faces flush, - pi / 2
	pivot_offset_ = 0;
	if (getFirstString(pivot_params, node_name) &&
	   !ros::NodeHandle(controller_nh, node_name).getParam("offset", pivot_offset_))
	{
		ROS_ERROR_STREAM("Can not read offset for pivot " << node_name);
		return false;
	}
	
	if (!pivot_joint_.initWithNode(talon_command_iface, nullptr, controller_nh, pivot_params))
	{
		ROS_ERROR("Can not initialize pivot joint(s)");
		return false;
	}

	if (!lift_joint_.initWithNode(talon_command_iface, nullptr, controller_nh, lift_params))
	{
		ROS_ERROR("Can not initialize lift joint(s)");
		return false;
	}
	if (!intake1_joint_.initWithNode(talon_command_iface, nullptr, controller_nh, intake1_params))
	{
		ROS_ERROR("Can not initialize intake1 joint(s)");
		return false;
	}
	if (!intake2_joint_.initWithNode(talon_command_iface, nullptr, controller_nh, intake2_params))
	{
		ROS_ERROR("Can not initialize intake2 joint(s)");
		return false;
	}
	
	if (!controller_nh.getParam("max_extension", max_extension_))
	{
		ROS_ERROR_NAMED(name_, "Can not read max_extension");
		return false;
	}
	if (!controller_nh.getParam("min_extension", min_extension_))
	{
		ROS_ERROR_NAMED(name_, "Can not read min_extension");
		return false;
	}

	if (!controller_nh.getParam("after_shift_max_accel", after_shift_max_accel_))
	{
		ROS_ERROR_NAMED(name_, "Can not read shift_max_accel");
		return false;
	}
	if (!controller_nh.getParam("after_shift_max_vel", after_shift_max_vel_))
	{
		ROS_ERROR_NAMED(name_, "Can not read shift_min_accel");
		return false;
	}

	if (!controller_nh.getParam("hook_depth", hook_depth_))
	{
		ROS_ERROR_NAMED(name_, "Can not read hook_depth");
		return false;
	}
	if (!controller_nh.getParam("hook_min_height", hook_min_height_))
	{
		ROS_ERROR_NAMED(name_, "Can not read hook_min_height");
		return false;
	}
	if (!controller_nh.getParam("hook_max_height", hook_max_height_))
	{
		ROS_ERROR_NAMED(name_, "Can not read hook_max_height");
		return false;
	}
	if (!controller_nh.getParam("custom_f_lift_high", f_lift_high_))
	{
		ROS_ERROR_NAMED(name_, "Can not read lift high");
		return false;
	}
	if (!controller_nh.getParam("custom_f_lift_low", f_lift_low_))
	{
		ROS_ERROR_NAMED(name_, "Can not read lift low");
		return false;
	}
	if (!controller_nh.getParam("custom_f_arm_mass", f_arm_mass_))
	{
		ROS_ERROR_NAMED(name_, "Can not read arm mass");
		return false;
	}
	if (!controller_nh.getParam("custom_f_arm_fric", f_arm_fric_))
	{
		ROS_ERROR_NAMED(name_, "Can not read arm friction");
		// TODO :: uncomment me as soon as I'm in a yaml file
		//return false;
	}
	
	double cut_off_y_line, cut_off_x_line, safe_to_go_back_y, drop_down_tolerance, drop_down_pos,
	dist_to_front_cube, dist_to_front_clamp, ready_to_go_into_intake_with_cube_x, ready_to_go_into_intake_with_cube_y, top_intake_cut_off_line_for_put_in_intake;


	if (!controller_nh.getParam("cut_off_y_line", cut_off_y_line))
	{
		ROS_ERROR_NAMED(name_, "Can not read cut_off_y_line");
		return false;
	}
	if (!controller_nh.getParam("cut_off_x_line", cut_off_x_line))
	{
		ROS_ERROR_NAMED(name_, "Can not read cut_off_x_line");
		return false;
	}
	if (!controller_nh.getParam("safe_to_go_back_y", safe_to_go_back_y))
	{
		ROS_ERROR_NAMED(name_, "Can not read safe_to_go_back_y ");
		return false;
	}
	if (!controller_nh.getParam("drop_down_tolerance", drop_down_tolerance))
	{
		ROS_ERROR_NAMED(name_, "Can not read drop_down_tolerance");
		return false;
	}
	if (!controller_nh.getParam("drop_down_pos", drop_down_pos))
	{
		ROS_ERROR_NAMED(name_, "Can not read drop_down_pos");
		return false;
	}
	if (!controller_nh.getParam("dist_to_front_cube", dist_to_front_cube))
	{
		ROS_ERROR_NAMED(name_, "Can not read dist_to_front_cube");
		return false;
	}
	if (!controller_nh.getParam("dist_to_front_clamp", dist_to_front_clamp))
	{
		ROS_ERROR_NAMED(name_, "Can not read dist_to_front_clamp");
		return false;
	}
	if (!controller_nh.getParam("ready_to_go_into_intake_with_cube_x", ready_to_go_into_intake_with_cube_x))
	{
		ROS_ERROR_NAMED(name_, "Can not read ready_to_go_into_intake_with_cube_x");
		return false;
	}
	if (!controller_nh.getParam("ready_to_go_into_intake_with_cube_y", ready_to_go_into_intake_with_cube_y))
	{
		ROS_ERROR_NAMED(name_, "Can not read ready_to_go_into_intake_with_cube_y");
		return false;
	}
	
	if (!controller_nh.getParam("top_intake_cut_off_line_for_put_in_intake", top_intake_cut_off_line_for_put_in_intake))
	{
		ROS_ERROR_NAMED(name_, "top_intake_cut_off_line_for_put_in_intake");
		return false;
	}
	
	//Set soft limits using offsets here
	lift_joint_.setForwardSoftLimitThreshold(max_extension_ + lift_offset_ + .03);
	lift_joint_.setReverseSoftLimitThreshold(min_extension_ + lift_offset_- .03);
	lift_joint_.setForwardSoftLimitEnable(true);
	lift_joint_.setReverseSoftLimitEnable(true);

	pivot_joint_.setForwardSoftLimitThreshold(M_PI  + pivot_offset_);
	pivot_joint_.setReverseSoftLimitThreshold(-1.05 + pivot_offset_);
	
	//TODO: something is broke with these soft limits

	pivot_joint_.setForwardSoftLimitEnable(true);
	pivot_joint_.setReverseSoftLimitEnable(true);
	
	//unit conversion will work using conversion_factor

	//TODO: something here to get bounding boxes etc for limits near bottom of drive train
	arm_limiting::polygon_type remove_zone_poly_down;
	std::vector<arm_limiting::point_type> point_vector_down;
	XmlRpc::XmlRpcValue poly_points_down;
	if (!controller_nh.getParam("polygon_points_down", poly_points_down))
	{
		ROS_ERROR_NAMED(name_, "Can not read polygon_points_down");
		return false;
	}
	point_vector_down.resize(poly_points_down.size()/2);
	//ROS_ERROR_NAMED(name_, "hypothetical errors");
	ROS_INFO_STREAM("Poly_points down" << std::endl << poly_points_down.size());
	for (int i = 0; i < poly_points_down.size()/2; ++i)
	{
		point_vector_down[i].x(static_cast<double>(poly_points_down[2*i]));
		point_vector_down[i].y(static_cast<double>(poly_points_down[2*i + 1]));
		ROS_INFO_STREAM("point from remove zone up: " << boost::geometry::wkt(point_vector_down[i]));
	}
	boost::geometry::assign_points(remove_zone_poly_down, point_vector_down);
	
	arm_limiting::polygon_type remove_zone_poly_up;
	std::vector<arm_limiting::point_type> point_vector_up;
	XmlRpc::XmlRpcValue poly_points_up;
	if (!controller_nh.getParam("polygon_points_up", poly_points_up))
	{
		ROS_ERROR_NAMED(name_, "Can not read polygon_points_up");
		return false;
	}
	point_vector_up.resize(poly_points_up.size()/2);
	//ROS_ERROR_NAMED(name_, "hypothetical errors");
	ROS_INFO_STREAM("Poly_points up" << std::endl << poly_points_up.size());
	for (int i = 0; i < poly_points_up.size()/2; ++i)
	{
		point_vector_up[i].x(static_cast<double>(poly_points_up[2*i]));
		point_vector_up[i].y(static_cast<double>(poly_points_up[2*i + 1]));
		ROS_INFO_STREAM("point from remove zone up: " << boost::geometry::wkt(point_vector_up[i]));
	}
	boost::geometry::assign_points(remove_zone_poly_up, point_vector_up);
	
	arm_limiting::polygon_type intake_up_box;
	std::vector<arm_limiting::point_type> point_vector_intake_up;
	XmlRpc::XmlRpcValue poly_points_intake_up;
	if (!controller_nh.getParam("polygon_points_intake_up", poly_points_intake_up))
	{
		ROS_ERROR_NAMED(name_, "Can not read polygon_points_intake_up");
		return false;
	}
	point_vector_intake_up.resize(poly_points_intake_up.size()/2);
	//ROS_ERROR_NAMED(name_, "hypothetical errors");
	ROS_INFO_STREAM("Poly_points intake_up" << std::endl << poly_points_intake_up.size());
	for (int i = 0; i < poly_points_intake_up.size()/2; ++i)
	{
		point_vector_intake_up[i].x(static_cast<double>(poly_points_intake_up[2*i]));
		point_vector_intake_up[i].y(static_cast<double>(poly_points_intake_up[2*i + 1]));
		ROS_INFO_STREAM("point from remove zone up: " << boost::geometry::wkt(point_vector_intake_up[i]));
	}
	boost::geometry::assign_points(intake_up_box, point_vector_intake_up);


	arm_limiting::polygon_type intake_down_box;
	std::vector<arm_limiting::point_type> point_vector_intake_down;
	XmlRpc::XmlRpcValue poly_points_intake_down;
	if (!controller_nh.getParam("polygon_points_intake_down", poly_points_intake_down))
	{
		ROS_ERROR_NAMED(name_, "Can not read polygon_points_intake_down");
		return false;
	}
	point_vector_intake_down.resize(poly_points_intake_down.size()/2);
	//ROS_ERROR_NAMED(name_, "hypothetical errors");
	ROS_INFO_STREAM("Poly_points intake_down" << std::endl << poly_points_intake_down.size());
	for (int i = 0; i < poly_points_intake_down.size()/2; ++i)
	{
		point_vector_intake_down[i].x(static_cast<double>(poly_points_intake_down[2*i]));
		point_vector_intake_down[i].y(static_cast<double>(poly_points_intake_down[2*i + 1]));
		ROS_INFO_STREAM("point from remove zone down: " << boost::geometry::wkt(point_vector_intake_down[i]));
	}
	boost::geometry::assign_points(intake_down_box, point_vector_intake_down);

	arm_limiting::polygon_type intake_in_transition_box;
	std::vector<arm_limiting::point_type> point_vector_intake_in_transition;
	XmlRpc::XmlRpcValue poly_points_intake_in_transition;
	if (!controller_nh.getParam("polygon_points_intake_in_transition", poly_points_intake_in_transition))
	{
		ROS_ERROR_NAMED(name_, "Can not read polygon_points_intake_in_transition");
		return false;
	}
	point_vector_intake_in_transition.resize(poly_points_intake_in_transition.size()/2);
	//ROS_ERROR_NAMED(name_, "hypothetical errors");
	ROS_INFO_STREAM("Poly_points intake_in_transition" << std::endl << poly_points_intake_in_transition.size());
	for (int i = 0; i < poly_points_intake_in_transition.size()/2; ++i)
	{
		point_vector_intake_in_transition[i].x(static_cast<double>(poly_points_intake_in_transition[2*i]));
		point_vector_intake_in_transition[i].y(static_cast<double>(poly_points_intake_in_transition[2*i + 1]));
		ROS_INFO_STREAM("point from remove zone in_transition: " << boost::geometry::wkt(point_vector_intake_in_transition[i]));
	}
	boost::geometry::assign_points(intake_in_transition_box, point_vector_intake_in_transition);

	arm_limiter_ = std::make_shared<arm_limiting::arm_limits>(min_extension_, max_extension_, 0.0, arm_length_, remove_zone_poly_down, remove_zone_poly_up, 15, cut_off_y_line, cut_off_x_line,  safe_to_go_back_y,  drop_down_tolerance,  drop_down_pos, hook_depth_, hook_min_height_, hook_max_height_, controller_nh, intake_up_box, intake_down_box, intake_in_transition_box, dist_to_front_cube, dist_to_front_clamp,ready_to_go_into_intake_with_cube_x, ready_to_go_into_intake_with_cube_y, top_intake_cut_off_line_for_put_in_intake);

	sub_command_ = controller_nh.subscribe("cmd_pos", 1, &ElevatorController::cmdPosCallback, this);
	sub_cube_ = controller_nh.subscribe("pub_cube_state", 1, &ElevatorController::cubeCallback, this);

	stop_arm_ = joint_state_iface->getHandle("stop_arm");

    /* NIALL */
	starting_config_ = joint_state_iface->getHandle("starting_config");
    /* NIALL */

	service_command_ = controller_nh.advertiseService("cmd_posS", &ElevatorController::cmdPosService, this);
	service_intake_ = controller_nh.advertiseService("intake", &ElevatorController::intakeService, this);
	service_clamp_ = controller_nh.advertiseService("clamp", &ElevatorController::clampService, this);
	service_shift_ = controller_nh.advertiseService("shift", &ElevatorController::shiftService, this);
	service_end_game_deploy_ = controller_nh.advertiseService("end_game_deploy", &ElevatorController::endGameDeployService, this);
	service_end_game_deploy_wings_ = controller_nh.advertiseService("end_game_deploy_wings", &ElevatorController::endGameDeployWingsService, this);

	Clamp_            = pos_joint_iface->getHandle("clamp");
	Shift_            = pos_joint_iface->getHandle("shift");
	EndGameDeploy_    = pos_joint_iface->getHandle("end_game_deploy");
	EndGameDeployWings_    = pos_joint_iface->getHandle("end_game_deploy_wings");
	IntakeUp_         = pos_joint_iface->getHandle("intake_up");
	IntakeSoftSpring_ = pos_joint_iface->getHandle("intake_spring_soft");
	IntakeHardSpring_ = pos_joint_iface->getHandle("intake_spring_hard");

	bot_lim_pub		  = controller_nh.advertise<std_msgs::Bool>("bottom_limit_pivot", 1);
	CubeState_        = controller_nh.advertise<elevator_controller::CubeState>("cube_state", 1);
	CubeStateJoint_   = pos_joint_iface->getHandle("cube_state");

	ReturnCmd_        = controller_nh.advertise<elevator_controller::ReturnElevatorCmd>("return_cmd_pos", 1);
	ReturnTrueSetpoint_ = controller_nh.advertise<elevator_controller::ReturnElevatorCmd>("return_true_setpoint", 1);

	Odom_             = controller_nh.advertise<elevator_controller::ReturnElevatorCmd>("odom", 1);

	before_shift_max_vel_ = lift_joint_.getMotionCruiseVelocity();
	before_shift_max_accel_ = lift_joint_.getMotionAcceleration();
	
	lift_joint_.setMotionAcceleration(before_shift_max_accel_);
	lift_joint_.setMotionCruiseVelocity(before_shift_max_vel_);
	lift_joint_.setPIDFSlot(0);
	
	lift_joint_.setContinuousCurrentLimit(norm_cur_lim_);

	return true;
}

void ElevatorController::update(const ros::Time &/*time*/, const ros::Duration &/*period*/)
{
	//const double delta_t = period.toSec();
	//const double inv_delta_t = 1 / delta_t;
	//compOdometry(time, inv_delta_t);
	const bool end_game_deploy_cmd = end_game_deploy_cmd_.load(std::memory_order_relaxed);
	const bool end_game_deploy_wings_cmd = end_game_deploy_wings_cmd_.load(std::memory_order_relaxed);

	const double deploy_wing_cmd_out = end_game_deploy_wings_cmd ? 1.0 : -1.0;
	
	EndGameDeployWings_.setCommand(deploy_wing_cmd_out);

	if(end_game_deploy_cmd && !end_game_deploy_t1_)
	{
		//ROS_INFO("part 1");
		IntakeCommand climb_intake_cmd;
		climb_intake_cmd.up_command = -1;
		climb_intake_cmd.spring_command = 1;
		climb_intake_cmd.power = 0;
        climb_intake_cmd.other_power = 0;
		
		intake_command_.writeFromNonRT(climb_intake_cmd); //Not really sure how bad this is

		command_struct_.lin[0] = 0.2;
		command_struct_.lin[1] = min_extension_ + cos(asin(0.2 / arm_length_))*arm_length_;
		command_struct_.up_or_down = true;
		command_struct_.override_pos_limits = false;
		command_struct_.override_sensor_limits = false;
		command_struct_.put_cube_in_intake = false;
		
		command_.writeFromNonRT(command_struct_);		

		end_game_deploy_t1_ = true;	
		end_game_deploy_start_ = ros::Time::now().toSec();
	}

	if(end_game_deploy_cmd && !end_game_deploy_t2_ && (ros::Time::now().toSec() - end_game_deploy_start_) > 2.5)
	{
		command_struct_.lin[0] = 0.05;
		command_struct_.lin[1] = min_extension_ + cos(asin(0.05 / arm_length_))*arm_length_;
		command_struct_.up_or_down = true;
		command_struct_.override_pos_limits = true;
		command_struct_.override_sensor_limits = false;
		command_struct_.put_cube_in_intake = false;
		
		command_.writeFromNonRT(command_struct_);		

		end_game_deploy_t2_time_ = ros::Time::now().toSec();
		end_game_deploy_t2_ = true;	

	}
	if(end_game_deploy_t2_ && (ros::Time::now().toSec() - end_game_deploy_t2_time_) > .65 && !end_game_deploy_t3_)
	{	
		//ROS_INFO("part 2");
		command_struct_.lin[0] = 0.05;
		command_struct_.lin[1] = max_extension_ + sin(acos(0.05 / arm_length_))*arm_length_;
		command_struct_.up_or_down = true;
		command_struct_.override_pos_limits = true;
		command_struct_.override_sensor_limits = false;
		command_struct_.put_cube_in_intake = false;

		command_.writeFromNonRT(command_struct_);		

		end_game_deploy_t3_ = true;
		//ROS_INFO("dropping");
	}
	if(end_game_deploy_cmd && (ros::Time::now().toSec() - end_game_deploy_start_) > 2.0)
	{	
		//ROS_INFO("dropping");
		EndGameDeploy_.setCommand(1.0);
	}
	else
	{
		EndGameDeploy_.setCommand(0.0);
	}

	bool local_shift_cmd = shift_cmd_.load(std::memory_order_relaxed);
	if(end_game_deploy_cmd && (ros::Time::now().toSec() - end_game_deploy_start_) > 4.5)
	{
		shift_cmd_.store(true, std::memory_order_relaxed);
		local_shift_cmd = true;
	} 
	if(local_shift_cmd)
	{	
		Shift_.setCommand(1.0);
		if(!shifted_)
		{
			shifted_ = true;
			lift_joint_.setMotionAcceleration(after_shift_max_accel_);
			lift_joint_.setMotionCruiseVelocity(after_shift_max_vel_);
			
			lift_joint_.setContinuousCurrentLimit(climb_cur_lim_);
			
			lift_joint_.setPIDFSlot(1);
			//lift_joint_.setF(f_lift_low_);
		}
	}
	else
	{
		Shift_.setCommand(-1.0);
		if(shifted_)
		{
			shifted_ = false;
			lift_joint_.setMotionAcceleration(before_shift_max_accel_);
			lift_joint_.setMotionCruiseVelocity(before_shift_max_vel_);
			
			lift_joint_.setContinuousCurrentLimit(norm_cur_lim_);

			lift_joint_.setPIDFSlot(0);
			//lift_joint_.setF(f_lift_high_);
		}
	}
	Commands curr_cmd = *(command_.readFromRT());
	const IntakeCommand cur_intake_cmd = *(intake_command_.readFromRT());
	//Use known info to write to hardware etc.
	//Put in intelligent bounds checking

	//ROS_INFO_STREAM("Intake power: " << cur_intake_cmd.power << " up?: " << cur_intake_cmd.up_command << " in state: " <<  cur_intake_cmd.spring_command);
	
	intake1_joint_.setCommand(cur_intake_cmd.power);
    if(cur_intake_cmd.power != cur_intake_cmd.other_power) 
    {
        intake2_joint_.setCommand(cur_intake_cmd.other_power);
    }
	else if(cur_intake_cmd.power > .5)
	{
		intake2_joint_.setCommand(cur_intake_cmd.power * intake_power_diff_multiplier_);
	}
	else
	{
		intake2_joint_.setCommand(cur_intake_cmd.power);

	}
	bool intake_up = cur_intake_cmd.up_command < 0;

	if(intake_up_last_ != intake_up)
	{
		transition_time_ = ros::Time::now().toSec();
	}
	bool in_transition = ros::Time::now().toSec() - transition_time_ < .5; //Consider making this 
																		   //check enable/disable 

	double intake_soft_msg;
	double intake_hard_msg;
	bool intake_open = false;
	switch(cur_intake_cmd.spring_command)
	{
		default:
			intake_soft_msg = 1.0;
			intake_hard_msg = 1.0;
			break;
		case 1:
			intake_soft_msg = 1.0;
			intake_hard_msg = -1.0;
			intake_open = true;
			break;
		case 3:
			intake_soft_msg = 0.0;
			intake_hard_msg = 1.0;
			break;
	}	
	IntakeSoftSpring_.setCommand(intake_soft_msg);
	IntakeHardSpring_.setCommand(intake_hard_msg);

	elevator_controller::CubeState cube_msg;
	if(overwritten)
	{
		cube_msg.intake_high = overwritten_cube.intake_high;
		cube_msg.intake_low = overwritten_cube.intake_low;
		cube_msg.clamp = overwritten_cube.clamp;
		cube_msg.has_cube = cube_msg.intake_high || cube_msg.intake_low || cube_msg.clamp;
	}
	else
	{
		cube_msg.intake_high = line_break_intake_high_.getPosition() != 0;
		cube_msg.intake_low = line_break_intake_low_.getPosition() != 0;
		cube_msg.clamp = pivot_joint_.getForwardLimitSwitch();
		cube_msg.has_cube = cube_msg.intake_high || cube_msg.intake_low || cube_msg.clamp;
	}
	CubeState_.publish(cube_msg);
	CubeStateJoint_.setCommand(cube_msg.has_cube ? 1.0 : 0.0);
	
	const double clamp_cmd = clamp_cmd_.load(std::memory_order_relaxed);
	
	Clamp_.setCommand(clamp_cmd);

	const double lift_position =  /*last_tar_l - lift_offset_*/lift_joint_.getPosition()  - lift_offset_;
	const double raw_pivot_angle   =  pivot_joint_.getPosition();
	
	const double adder = floor(((raw_pivot_angle - pivot_offset_) + M_PI) / (2.0 * M_PI)) * 2.0 * M_PI;
	if(fabs(adder) > .1)    //Offset will jump by intervals of 2 * pi, 
							//don't reset soft limits if change is just due to floating
							//point error
	{
		pivot_offset_ += adder;
		pivot_joint_.setForwardSoftLimitThreshold(M_PI + pivot_offset_);
		pivot_joint_.setReverseSoftLimitThreshold(-1.05 + pivot_offset_);
		ROS_WARN("Pivot encoder discontinuouity detected and accounted for");
	
	}
	const double pivot_angle = raw_pivot_angle - pivot_offset_;

	//ROS_WARN_STREAM("ang: " << pivot_angle << " add " << adder);

	//TODO: put in similar checks for the lift using limit switches
	bool cur_up_or_down = pivot_angle > 0;

	arm_limiting::point_type cur_pos(cos(pivot_angle)*arm_length_, lift_position +
			sin(pivot_angle)*arm_length_);

	elevator_controller::ReturnElevatorCmd odom_holder;
	odom_holder.x = cur_pos.x();
	odom_holder.y = cur_pos.y();
	odom_holder.up_or_down = cur_up_or_down;

	Odom_.publish(odom_holder);
	bool stop;
	if(stop_arm_.getPosition())
	{	
		stop = true;
		pivot_joint_.setPeakOutputForward(0);
		pivot_joint_.setPeakOutputReverse(0);

		lift_joint_.setPeakOutputForward(0);
		lift_joint_.setPeakOutputReverse(0);
	}
	else
	{
		stop = false;
		pivot_joint_.setPeakOutputForward(1);
		pivot_joint_.setPeakOutputReverse(-1);

		lift_joint_.setPeakOutputForward(1);
		lift_joint_.setPeakOutputReverse(-1);
	}
    /* NIALL */
    if(starting_config_.getPosition())
    {
		
		command_struct_.lin[0] = 0.05;
		command_struct_.lin[1] = min_extension_ + sin(acos(0.05 / arm_length_))*arm_length_ + .05;
		command_struct_.up_or_down = true;
		command_struct_.override_pos_limits = true;
		command_struct_.override_sensor_limits = false;
		command_struct_.put_cube_in_intake = false;

		command_.writeFromNonRT(command_struct_);		
    }
    else
    {
    
    }
    /* NIALL */
	std_msgs::Bool bottom_limit;
	bottom_limit.data = pivot_joint_.getReverseLimitSwitch(); //TODO FIX THIS
	bot_lim_pub.publish(bottom_limit);
	if(bottom_limit.data && !stop)
	{	
		
		pivot_joint_.setPeakOutputReverse(-limit_power_press_);

	}
	bool safe_to_move_intake;
	elevator_controller::ReturnElevatorCmd return_holder;
	if(!curr_cmd.override_pos_limits)
	{
		arm_limiting::point_type cmd_point(curr_cmd.lin[0], curr_cmd.lin[1]);

		bool reassignment_holder;

		arm_limiting::point_type return_cmd;
		bool return_up_or_down;
		const bool cube_in_clamp = cube_msg.clamp && (clamp_cmd <= 0);
		arm_limiter_->safe_cmd(cmd_point, curr_cmd.up_or_down, reassignment_holder, cur_pos, cur_up_or_down, return_cmd, return_up_or_down, bottom_limit.data, intake_up, in_transition, safe_to_move_intake, cube_in_clamp, intake_open, curr_cmd.put_cube_in_intake);

		return_holder.x = return_cmd.x();
		return_holder.y = return_cmd.y();
		return_holder.up_or_down = return_up_or_down;

		//potentially do something if reassignment is needed (Like a ROS_WARN?)

		curr_cmd.lin[0] = cmd_point.x();
		curr_cmd.lin[1] = cmd_point.y();
	}
	else
	{
		return_holder.x = curr_cmd.lin[0];
		return_holder.y = curr_cmd.lin[1];
		return_holder.up_or_down = curr_cmd.up_or_down;
		safe_to_move_intake = true;
	
	/*	
	//___________________TESTING________________________________\\

		arm_limiting::point_type cmd_point(curr_cmd.lin[0], curr_cmd.lin[1]);

		bool reassignment_holder;

		arm_limiting::point_type return_cmd;
		bool return_up_or_down;
		bool bottom_limit = false; //TODO FIX THIS
		const bool cube_in_clamp = cube_msg.clamp && (clamp_cmd <= 0);
		bool copy_up_or_down = curr_cmd.up_or_down;
		arm_limiter_->safe_cmd(cmd_point, copy_up_or_down, reassignment_holder, cur_pos, cur_up_or_down, return_cmd, return_up_or_down, bottom_limit, intake_up, in_transition, safe_to_move_intake, cube_in_clamp, intake_open, curr_cmd.put_cube_in_intake);


		//potentially do something if reassignment is needed (Like a ROS_WARN?)


	//___________________TESTING________________________________\\
	*/


	}
	return_holder.header.stamp = ros::Time::now();
	ReturnCmd_.publish(return_holder);

	if(!curr_cmd.override_sensor_limits)
	{
		//TODO: something here which reads time of flight/ultrasonic pos
		//will only go up/down to within 15 cm
		//if target is beyond dist, will bring arm all the way up or down to go around
		//this is relatively low priority
	}

	if(safe_to_move_intake)
	{
		intake_up_last_ = intake_up;
	}
	else
	{
		intake_up = intake_up_last_;
	}
	if(intake_up)
	{
		IntakeUp_.setCommand(-1.0);
		intake_down_time_.store(ros::Time::now().toSec(), std::memory_order_relaxed);
	}
	else
	{
		//if((ros::Time::now().toSec() - intake_down_time_.load(std::memory_order_relaxed)) < 1.5) //1.5 is super arbitary
		//{
			IntakeUp_.setCommand(1.0);
		//}
		/*else
		{
			IntakeUp_.setCommand(0);
		}*/
	}
	//Delay stuff maybe?

	elevator_controller::ReturnElevatorCmd final_cmd_holder;
	final_cmd_holder.x = curr_cmd.lin[0];
	final_cmd_holder.y = curr_cmd.lin[1];
	final_cmd_holder.up_or_down = curr_cmd.up_or_down;

	ReturnTrueSetpoint_.publish(final_cmd_holder);
	//ROS_INFO_STREAM("cmd: " << curr_cmd.lin << " up/down: " << curr_cmd.up_or_down);
	const double pivot_target = acos(curr_cmd.lin[0]/arm_length_) * ((curr_cmd.up_or_down) ? 1 : -1);
	
	//ROS_INFO_STREAM("up_or_down: " << curr_cmd.up_or_down << "lin pos target" << curr_cmd.lin << " lift pos tar: " << curr_cmd.lin[1] - arm_length_ * sin(pivot_target));	
	double pivot_custom_f = cos(pivot_angle) * f_arm_mass_ +f_arm_fric_;

	//ROS_WARN_STREAM("setting: " << pivot_target << " actual: " << pivot_target + pivot_offset_);

	pivot_joint_.setCommand(pivot_target + pivot_offset_);
	lift_joint_.setCommand(curr_cmd.lin[1] - arm_length_ * sin(pivot_target) + lift_offset_);
	
	last_tar_l = curr_cmd.lin[1] - arm_length_ * sin(pivot_target) + lift_offset_;
	last_tar_p = (pivot_target + pivot_offset_);
	overwritten = false;
}
void ElevatorController::starting(const ros::Time &/*time*/)
{
	//maybe initialize the target to something if not otherwise set?
}
void ElevatorController::cmdPosCallback(const elevator_controller::ElevatorControl &command)
{
	Commands command_struct;
	if(isRunning())
	{
		command_struct.lin[0] = command.x;
		command_struct.lin[1] = command.y;
		command_struct.up_or_down = command.up_or_down;
		command_struct.override_pos_limits = command.override_pos_limits;
		command_struct.override_sensor_limits = command.override_sensor_limits;
		command_struct.put_cube_in_intake = command.put_cube_in_intake;

		command_struct.stamp = ros::Time::now();
		command_.writeFromNonRT(command_struct);
	}
	else
	{
		ROS_ERROR_NAMED(name_, "Can't accept new commands. Controller is not running.");
	}
}

void ElevatorController::cubeCallback(const elevator_controller::CubeState::ConstPtr published_state)
{
	ROS_INFO_STREAM("running this callback whether you like it or not");
	if(isRunning())
	{
		overwritten_cube.intake_high = published_state->intake_high;
		overwritten_cube.intake_low = published_state->intake_low;
		overwritten_cube.clamp = published_state->clamp;
		overwritten_cube.has_cube = overwritten_cube.intake_high || overwritten_cube.intake_low || overwritten_cube.clamp;
		if(published_state != NULL)
			overwritten = true;
	}
	else
	{
		ROS_ERROR_NAMED(name_, "Can't accept new commands. Controller is not running.");
	}
}

bool ElevatorController::cmdPosService(elevator_controller::ElevatorControlS::Request &command, elevator_controller::ElevatorControlS::Response &/*res*/)
{
	Commands command_struct;
	if(isRunning())
	{
		command_struct.lin[0] = command.x;
		command_struct.lin[1] = command.y;
		command_struct.up_or_down = command.up_or_down;
		command_struct.override_pos_limits = command.override_pos_limits;
		command_struct.override_sensor_limits = command.override_sensor_limits;

		command_struct.put_cube_in_intake = command.put_cube_in_intake;
		command_struct.stamp = ros::Time::now();
		command_.writeFromNonRT(command_struct);
		return true;
	}
	else
	{
		ROS_ERROR_NAMED(name_, "Can't accept new commands. Controller is not running.");
		return false;
	}
}

bool ElevatorController::clampService(std_srvs::SetBool::Request &command, std_srvs::SetBool::Response &/*res*/)
{
	if(isRunning())
	{
		if(command.data)
		{
			clamp_cmd_.store(-1.0, std::memory_order_relaxed);
		}
		else
		{
			clamp_cmd_.store(1.0, std::memory_order_relaxed);
		}
		return true;
	}
	else
	{
		ROS_ERROR_NAMED(name_, "Can't accept new commands. Controller is not running.");
		return false;
	}
}

bool ElevatorController::shiftService(std_srvs::SetBool::Request &command, std_srvs::SetBool::Response &/*res*/)
{
	if(isRunning())
	{
		shift_cmd_.store(command.data, std::memory_order_relaxed);
		return true;
	}
	else
	{
		ROS_ERROR_NAMED(name_, "Can't accept new commands. Controller is not running.");
		return false;
	}
}

bool ElevatorController::endGameDeployService(std_srvs::Empty::Request &/*command*/, std_srvs::Empty::Response &/*res*/)
{
	if(isRunning())
	{
		ROS_WARN("called deploy");
		end_game_deploy_cmd_.store(true, std::memory_order_relaxed);
		return true;
	}
	else
	{
		ROS_ERROR_NAMED(name_, "Can't accept new commands. Controller is not running.");
		return false;
	}
}
bool ElevatorController::endGameDeployWingsService(std_srvs::Empty::Request &/*command*/, std_srvs::Empty::Response &/*res*/)
{
	if(isRunning())
	{
		ROS_WARN("called wings deploy");
		end_game_deploy_wings_cmd_.store(true, std::memory_order_relaxed);
		return true;
	}
	else
	{
		ROS_ERROR_NAMED(name_, "Can't accept new commands. Controller is not running.");
		return false;
	}
}

bool ElevatorController::intakeService(elevator_controller::Intake::Request &command, elevator_controller::Intake::Response &/*res*/)
{
	if(isRunning())
	{
		IntakeCommand intake_struct;
		intake_struct.power = command.power;
		intake_struct.other_power = command.power;
		if(!command.just_override_power)
		{
			if(command.up)
			{
				intake_struct.up_command = -1.0;
			}
			else
			{
				intake_struct.up_command = 1.0;
			}

			intake_struct.spring_command = command.spring_state;
		}
		else
		{
            ROS_WARN("intake called");
			const IntakeCommand cur_intake_cmd = *(intake_command_.readFromRT());
			intake_struct.up_command = cur_intake_cmd.up_command;
			intake_struct.spring_command = cur_intake_cmd.spring_command;
		}
		intake_command_.writeFromNonRT(intake_struct);
		intake_down_time_.store(ros::Time::now().toSec(), std::memory_order_relaxed);
		return true;
	}
	else
	{
		ROS_ERROR_NAMED(name_, "Can't accept new commands. Controller is not running.");
		return false;
	}
}
}//Namespace
