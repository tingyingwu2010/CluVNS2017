#include "Heuristic.h"
#include "Move.h"

Heuristic::Heuristic(CluVRPinst* cluVRPinst)
{
	this->cluVRPinst_ = cluVRPinst;
}

Heuristic::~Heuristic(){}

/******************************** BIN PACKING ****************************************/

//继承自Heuristic;方法:run;私有;Redistribution和Diversification
BinPacking::BinPacking(CluVRPinst* cluVRPinst) : Heuristic(cluVRPinst)
{
	redistributionOperator_ = new Redistribution(cluVRPinst);
	diversOperator_ = new Diversification(cluVRPinst);
}

BinPacking::~BinPacking()
{
	delete redistributionOperator_;
	delete diversOperator_;
}

//应该是装箱算法，如BF等；但仅返回bool型,ClusterSolution会在函数中更新
bool BinPacking::run(ClusterSolution*& s)
{
	//获取inst算例中的Clusters数据,不包括depot的cluster
	std::vector<Cluster*> vClientClusters = cluVRPinst_->getClientClusters();

	//sort clusters from by demand (large to small)
	sort(vClientClusters.begin(), vClientClusters.end(), Cluster::sortByDemandOperator);

	//create trip for every vehicle,每个vehicle对应一个trip,该段仅包含初始depot的trip与包含该trip的CluSolution
	for (int i = 0; i < this->cluVRPinst_->getnVehicles(); i++)
	{
		CluTrip* t = new CluTrip(cluVRPinst_);	//每个Trip包含算例;dist;totalDemand;size
		
		//select random depot	
		//每个算例inst均有vClusters_,可能含有多个depot聚类,随机选择一个作为初始Trip的起点
		t->addStop(cluVRPinst_->getRandomDepot());	//t是一个CluTrip;addStop:增加Cluster到该CluTrip
		//add trip to the solution
		s->addTrip(t);	//s是一个ClusterSolution,加入Trip t
	}

	//add the clusters one by one
	int nRedistributionIterations = 0;

	//循环选择cluster放入不同trip,放到解s中,直到放完
	while (vClientClusters.size() > 0)
	{
		//get cluster to add
		//c将被放入某个trip/veh中
		Cluster* c = vClientClusters.front();

		//get strategy
		double r = (double)rand() / RAND_MAX;
		bool success = false;	
		int veh;

		//依据概率,随机选择veh(要求剩余容量足够)或选择距当前类c最近的veh(要求剩余容量足够,且距离最近的veh/vtrips)
		if (r < Params::RANDOM_CONSTRUCTION) veh = s->getRandomFeasibleVehicle(c,success);	
		else veh = s->getClosestFeasibleVehicle(c,success);	//RANDOM_CONSTRUCTION默认为0,因此全部选择最近的

		//perform move
		//判断是否可以找到容量足够的车,如果找到,将c放入该veh对应的trip;删除c
		if (success)
		{
			s->getTrip(veh)->addStop(c);
			vClientClusters.erase(vClientClusters.begin());
		}
		else	//如果未找到,调用Sec3.3的redistribution algorithm(SwapCluster),破解无车可用情形
		{			
			redistributionOperator_->run(s);
			nRedistributionIterations++;
			
			if (nRedistributionIterations > 5)	//如果redistribution次数超过5次,执行diversOperator(perturbationCluster + repairCluster)
			{
				if (nRedistributionIterations > 100) return false;	//如果redistribution次数超过100次,返回BF算法执行失败
				diversOperator_->run(s);	//执行Sec3.5节
			}
		}
	}

	//return to the initial depot
	//增加size及返回到depot的距离到ClusterSolution的s中
	for (int i = 0; i < this->cluVRPinst_->getnVehicles(); i++)
	{
		s->getTrip(i)->returnToDepot();
	}

	//calc objective value of this solution
	//计算ClusterSolution的s的目标值,总距离totalDist_,每个trip的距离之和
	s->calcTotalDist();

	return true;
}

/******************************* DIVERSIFICATION **************************************/

void Diversification::repairCluster(ClusterSolution*& s, std::vector<Cluster*> toAdd)
{
	//add clusters in random order
	random_shuffle(toAdd.begin(), toAdd.end());
	int nTry = 0;
	
	while (toAdd.size() > 0)
	{
		std::vector<int> feasVeh;

		//look for all feasible vehicles (capacity)
		do {

			for (int v = 0; v < s->getnTrips(); v++)
			{
				if (toAdd.back()->getDemand() <= s->getTrip(v)->getSpareCapacity())
				{
					feasVeh.push_back(v);
				}
			}

			if (feasVeh.size() == 0)	//no feasible vehicles found
			{
				redistribution_->run(s);
				nTry++;
				if (nTry > 100)
				{
					//add an additional trip
					CluTrip* t = new CluTrip(cluVRPinst_);
					//add random depot
					t->addStop(cluVRPinst_->getRandomDepot());
					s->addTrip(t);
					cluVRPinst_->increasenVeh();

					continue;
				}
				else if (nTry > 5)
				{
					std::vector<Cluster*> removed;
					removed = perturbationCluster(s);
					toAdd.insert(toAdd.end(), removed.begin(), removed.end());
					continue;
				}
			}
			else break;

		} while (true);

		//choose one feasible vehicle at random
		int selection = rand() % feasVeh.size();
		s->getTrip(feasVeh.at(selection))->addStop(toAdd.back());
		toAdd.pop_back();
	}
	

	//return to the depot for every trip
	for (int v = 0; v < cluVRPinst_->getnVehicles(); v++)
	{
		s->getTrip(v)->addStop(s->getTrip(v)->getvClusters().front());
	}

	s->calcTotalDist();
}

std::vector<Cluster*> Diversification::perturbationCluster(ClusterSolution*& s)
{
	std::vector<Cluster*> removedClusters;

	for (int v = 0; v < cluVRPinst_->getnVehicles(); v++)
	{
		if (s->getTrip(v)->getvClusters().front() == s->getTrip(v)->getvClusters().back())
		{
			s->getTrip(v)->removeStop(s->getTrip(v)->getSize() - 1);		//remove depot at the end of the trip
		}

		for (int i = 1; i < s->getTrip(v)->getSize(); i++)
		{
			if (s->getTrip(v)->getSize() < 3)
			{
				break;
			}
			double r = (double)rand() / (RAND_MAX);
			if (r < Params::PERT_RATE)
			{
				removedClusters.push_back(s->getTrip(v)->getCluster(i));
				s->getTrip(v)->removeStop(i);
			}
		}
	}

	return removedClusters;
}

Diversification::Diversification(CluVRPinst* cluVRPinst) : Heuristic (cluVRPinst)
{
	redistribution_ = new Redistribution(cluVRPinst_);
}

Diversification::~Diversification() 
{
	delete redistribution_;
}

bool Diversification::run(ClusterSolution*& s)
{
	std::vector<Cluster*> toAdd;

	toAdd = perturbationCluster(s);
	repairCluster(s, toAdd);

	double r = (double)rand() / RAND_MAX;
	if (r < Params::DIVERSIFICATION_1)
		return false;		//go to cluVNS
	else
		return true;		//go to conversion
}

/******************************* REDISTRIBUTION **************************************/

bool Redistribution::swapClusters(ClusterSolution*& cluSol)
{
	CluTrip* cluT1 = nullptr;
	CluTrip* cluT2 = nullptr;

	int minDif = BIG_M;
	bool success = false;
	bool flag = false;

	for (int v1 = 0; v1 < cluVRPinst_->getnVehicles(); v1++)
	{
		cluT1 = cluSol->getTrip(v1);

		int spareCapacity = cluT1->getSpareCapacity();
		if (spareCapacity == 0) continue;

		for (int v2 = 0; v2 < cluVRPinst_->getnVehicles(); v2++)
		{
			if (v1 == v2) continue;

			cluT2 = cluSol->getTrip(v2);

			for (int i = 1; i < cluT1->getSize() - 1; i++)
			{
				for (int j = 1; j < cluT2->getSize() - 1; j++)
				{
					if (cluT1->getCluster(i)->getDemand() == cluT2->getCluster(j)->getDemand()) continue;

					int diff = \
						+ spareCapacity\
						+ cluT1->getCluster(i)->getDemand()\
						- cluT2->getCluster(j)->getDemand();

					if (diff >= 0 && diff < minDif)
					{
						Pos p1(v1, i);
						Pos p2(v2, j);
						if ((p1 == p1_ && p2 == p2_) || (p1 == p2_ && p2 == p1_)) continue;
						
						success = true;
						minDif = diff;
						p1_ = p1;
						p2_ = p2;
						if (minDif == 0)
						{
							flag = true;
							break;
						}
					}
				}
				if (flag) break;
			}
			if (flag) break;
		}
		if (flag) break;
	}
	if (success)
	{
		int c = \
			+ cluSol->getTrip(p1_.veh_)->getCluster(p1_.ind_)->getDemand()\
			- cluSol->getTrip(p2_.veh_)->getCluster(p2_.ind_)->getDemand();

		cluSol->getTrip(p1_.veh_)->altTotalDemand(-c);
		cluSol->getTrip(p2_.veh_)->altTotalDemand(c);

		cluSol->getTrip(p1_.veh_)->altDist(\
			- cluVRPinst_->getDistClusters(cluSol->getTrip(p1_.veh_)->getCluster(p1_.ind_ - 1), cluSol->getTrip(p1_.veh_)->getCluster(p1_.ind_))\
			- cluVRPinst_->getDistClusters(cluSol->getTrip(p1_.veh_)->getCluster(p1_.ind_), cluSol->getTrip(p1_.veh_)->getCluster(p1_.ind_ + 1))\
			+ cluVRPinst_->getDistClusters(cluSol->getTrip(p1_.veh_)->getCluster(p1_.ind_ - 1), cluSol->getTrip(p2_.veh_)->getCluster(p2_.ind_))\
			+ cluVRPinst_->getDistClusters(cluSol->getTrip(p2_.veh_)->getCluster(p2_.ind_), cluSol->getTrip(p1_.veh_)->getCluster(p1_.ind_ + 1)));

		cluSol->getTrip(p2_.veh_)->altDist(\
			- cluVRPinst_->getDistClusters(cluSol->getTrip(p2_.veh_)->getCluster(p2_.ind_ - 1), cluSol->getTrip(p2_.veh_)->getCluster(p2_.ind_))\
			- cluVRPinst_->getDistClusters(cluSol->getTrip(p2_.veh_)->getCluster(p2_.ind_), cluSol->getTrip(p2_.veh_)->getCluster(p2_.ind_ + 1))\
			+ cluVRPinst_->getDistClusters(cluSol->getTrip(p2_.veh_)->getCluster(p2_.ind_ - 1), cluSol->getTrip(p1_.veh_)->getCluster(p1_.ind_))\
			+ cluVRPinst_->getDistClusters(cluSol->getTrip(p1_.veh_)->getCluster(p1_.ind_), cluSol->getTrip(p2_.veh_)->getCluster(p2_.ind_ + 1)));

		Move::swap(cluSol, p1_, p2_);
		return true;
	}
	else
	{
		return false;
	}
}

Redistribution::Redistribution(CluVRPinst* cluVRPinst) : Heuristic(cluVRPinst)
{
	p1_ = Pos(-1, -1);
	p2_ = Pos(-1, -1);
}

Redistribution::~Redistribution() {}

bool Redistribution::run(ClusterSolution*& cluSol)
{
	return swapClusters(cluSol);
}