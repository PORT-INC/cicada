// © 2016 PORT INC.

#include <cstdio>
#include <limits>
#include <iostream>
#include <fstream>
#include <sstream>
#include <list>
#include <boost/lexical_cast.hpp>
#include <boost/format.hpp>
#include <boost/numeric/ublas/io.hpp>
#include <cassert>
#include <cmath>
#include <clocale>
#include <mecab.h>
#include "SemiCrf.hpp"
#include "AppTest.hpp"
#include "MultiByteTokenizer.hpp"
#include "Logger.hpp"
#include "Error.hpp"
#include "ujson.hpp"
#include "JsonIO.hpp"

namespace SemiCrf {

	//// Factories ////

	decltype(std::make_shared<Labels>()) createLabels(int size)
	{
		return std::make_shared<Labels>(size);
	}

	decltype(std::make_shared<Segment>()) createSegment(int start, int end, Label label)
	{
		return std::make_shared<Segment>(start, end, label);
	}

	decltype(std::make_shared<Segments>()) createSegments()
	{
		return std::make_shared<Segments>();
	}

	CheckTable createCheckTable(int capacity)
	{
		return std::make_shared<CheckTable_>(capacity, CheckTuple());
	}

	CheckVTable createCheckVTable(int capacity)
	{
		return std::make_shared<CheckVTable_>(capacity, CheckPair());
	}

	CacheTable createCacheTable(int capacity)
	{
		return std::make_shared<CacheTable_>(capacity, CacheTuple());
	}

	//// Weights ////

	decltype(std::shared_ptr<Weights>()) createWeights(int dim)
	{
		return std::make_shared<Weights>(dim);
	}

	Weights::Weights(int dim)
		: std::vector<double>(dim)
	{
		Logger::debug() << "Weights()";
	}

	Weights::~Weights()
	{
		Logger::debug() << "~Weights()";
	}

	void Weights::readJson(std::istream& is)
	{
		auto object = JsonIO::parse(is);
		auto title = JsonIO::readString(object, "title");
		auto dims = JsonIO::readIntAry(object, "dimension");
		xDim = dims[0];
		yDim = dims[1];
		feature = JsonIO::readString(object, "feature");
		maxLength = JsonIO::readInt(object, "max_length");
		mean = JsonIO::readIntDoubleMap(object, "mean");
		variance = JsonIO::readIntDoubleMap(object, "variance");
		auto weight = JsonIO::readDoubleAry(object, "weights");
		for( auto w : weight ) push_back(w);
	}

	void Weights::read(std::istream& ifs)
	{
		Logger::debug() << "Weights::read()";

		readJson(ifs);
		if( empty() ) {
			throw Error("empty weights");
		}
	}

	void Weights::writeJson(std::ostream& ofs)
	{
		Logger::debug() << "Weights::writeJson()";

		ujson::array jweights;
		for( auto& w : *this ) {
			jweights.push_back(w);
		}

		ujson::array jmeans;
		for( auto& m : mean ) {
			ujson::array jmean;
			jmean.push_back(m.first);
			jmean.push_back(m.second);
			jmeans.push_back(std::move(jmean));
		}

		ujson::array jvariancies;
		for( auto& v : variance ) {
			ujson::array jvariance;
			jvariance.push_back(v.first);
			jvariance.push_back(v.second);
			jvariancies.push_back(std::move(jvariance));
		}

		auto object = ujson::object {
			{ "title", "Semi-CRF Weights" },
			{ "dimension", std::move(ujson::array{ xDim, yDim }) },
			{ "feature", feature },
			{ "max_length", maxLength },
			{ "mean", jmeans },
			{ "variance", jvariancies },
			{ "weights", jweights }
		};

		ofs << to_string(object) << std::endl;
	}

	void Weights::write(std::ostream& ofs)
	{
		Logger::debug() << "Weights::write()";
		writeJson(ofs);
	}

	FeatureFunction::FeatureFunction()
	{
		Logger::debug() << "FeatureFunction()";
	}

	FeatureFunction::~FeatureFunction()
	{
		Logger::debug() << "~FeatureFunction()";
	}

	//// Algorithm ////

	Algorithm::Algorithm(int arg)
		: flg(arg)
	{
		Logger::debug() << "Algorithm()";
		if( !(flg & DISABLE_DATE_VERSION) ) {
			Logger::info() << "cicada 0.0.1";
			Logger::info() << "Copyright (C) 2016 PORT, Inc.";
		} else {
			Logger::info() << "cicada";
		}
	}

	Algorithm::~Algorithm()
	{
		Logger::debug() << "~Algorithm()";
	}

	void Algorithm::setLabels(decltype(labels) arg)
	{
		labels = arg;
	}

	void Algorithm::setMaxLength(int arg)
	{
		maxLength = arg;
	}

	void Algorithm::setMaxIteration(int arg)
	{
		maxIteration = arg;
	}

	void Algorithm::setE0(double arg)
	{
		e0 = arg;
	}

	void Algorithm::setE1(double arg)
	{
		e1 = arg;
	}

	void Algorithm::setRp(double arg)
	{
		rp = arg;
	}

	void Algorithm::setMethod(const std::string& arg)
	{
		method = arg;
	}

	void Algorithm::setDatas(decltype(datas) arg)
	{
		datas = arg;
	}

	void Algorithm::setFeatureFunction(decltype(ff) arg)
	{
		ff = arg;
	}

	void Algorithm::setWeights(decltype(weights) arg)
	{
		weights = arg;
	}

	void Algorithm::setDimension(decltype(dim) arg)
	{
		dim = arg;
	}

	double Algorithm::computeWG(Label y, Label yd, int i, int d, uvector& gs)
	{
		double v = 0.0;

		if( ! (flg & DISABLE_WG_CACHE) ) {

			int l = labels->size();
			int s = current_data->getStrs()->size();
			int idx = y*l*s*maxLength + yd*s*maxLength + i*maxLength + d - 1;
			int p = idx % cacheSize;
			auto& tp = current_wgtab->at(p);

			if( std::get<0>(tp) == idx && std::get<2>(tp) != nullptr ) {

				v = std::get<1>(tp);
				gs = *std::get<2>(tp);
				++hit;

			} else {

				v = ff->wg(*weights, y, yd, *current_data, i-d+1, i, gs);
				std::get<0>(tp) = idx;
				std::get<1>(tp) = v;
				std::get<2>(tp) = std::make_shared<uvector>(gs); // gsをコピーしてshared_ptrを作る
				++miss;
			}

		} else {

			v = ff->wg(*weights, y, yd, *current_data, i-d+1, i, gs);

		}

		return v;
	}

	//// Learner ////

	decltype( std::make_shared<Algorithm>() ) createLearner(int arg)
	{
		return std::make_shared<Learner>(arg);
	}

	Learner::Learner(int arg)
		: Algorithm(arg)
	{
		Logger::debug() << "Learner()";
		if( !(flg & ENABLE_LIKELIHOOD_ONLY) ) {
			Logger::info() << "Learning ...";
		}
	}

	Learner::~Learner()
	{
		Logger::debug() << "~Learner()";
	}

	void Learner::preProcess
	(     const std::string& wfile
		, const std::string& w0file
		, const std::string& w2vfile
		)
	{
		// datasからx,yの次元、featureを取得する
		int xdim = datas->getXDim();
		int ydim = datas->getYDim();
		const auto& feature = datas->getFeature();

		// feature関数を生成し、x,yの次元、feature、maxLengthを設定する
		ff = App::createFeatureFunction(feature, w2vfile);
		try {
			ff->setXDim(xdim);
		} catch (Error& e) {
			throw Error("dimension mismatch between data file and w2v mastrix file");
		}
		ff->setYDim(ydim);
		ff->setMaxLength(maxLength); // maxLengthはdatasをreadした直後に設定されている

		// featureから次元を取得しアルゴリズムに設定
		dim = ff->getDim();

		// 重みを生成しアルゴリズムに設定
		auto weights = SemiCrf::createWeights(dim);
		setWeights(weights);

		if( !w0file.empty() ) { // 初期重みが指定されている場合
			std::ifstream ifs; // 入力
			open(ifs, w0file);
			weights->resize(0);
			weights->read(ifs); // 重みを初期重みで初期化
		}

		// 重みにfeatureを設定
		weights->setFeature(feature);

		// ラベルを生成
		auto labels = createLabels(ydim);
		setLabels(labels);

		// 作業領域を初期化
		gs.resize(dim);
	}

	void Learner::postProcess(const std::string& wfile)
	{
		if( !(flg & ENABLE_LIKELIHOOD_ONLY) ) {
			std::ofstream ofs; // 出力
			open(ofs, wfile);
			weights->setXDim(datas->getXDim());
			weights->setYDim(datas->getYDim());
			weights->setMaxLength(maxLength);
			weights->setMean(datas->getMean());
			weights->setVariance(datas->getVariance());
			weights->write(ofs);
		}
	}

	void Learner::compute()
	{
		Logger::debug() << "Learner::compute()";

		if( !(flg & ENABLE_LIKELIHOOD_ONLY) ){

			auto ofunc = createLikelihood(this);
			Optimizer::UnconstrainedNLP optimizer;

			if( method == "bfgs" ) {
				optimizer = createBfgs(dim, ofunc);
			} else if( method == "steepest_decent" ) {
				optimizer = createSteepestDescent(dim, ofunc);
			} else {
				std::stringstream ss;
				ss << "unknown method specifed: " << method;
				throw Error(ss.str());
			}

			int f = 0x0;
			if( !(DISABLE_ADAGRAD & flg) ){
				f |= Optimizer::ENABLE_ADAGRAD;
			}

			optimizer->setFlg(f);
			optimizer->setE0(e0);
			optimizer->setRe(e1);
			optimizer->setAe(e1);
			optimizer->setMaxIteration(maxIteration);
			optimizer->optimize();

		} else {

			double L = 0.0;
			std::vector<double> dL(dim, 0.0);
			computeGrad(L, dL);
		}
	}

	void Learner::computeGrad(double& L, std::vector<double>& dL, bool grad)
	{
		for( auto data : *datas ) {

			current_data = data;
			current_wgtab = createCacheTable(cacheSize);
			hit = miss = 0;

			double WG = 0.0;
			auto Z = computeZ();
			auto Gs = computeG(WG);

			L += WG - log(Z);

			if( !(flg & DISABLE_REGULARIZATION) ) {
				double w2 = 0.0;
				for( auto w : *weights ) {
					w2 += w*w;
				}
				w2 *= rp;
				L -= w2;
			}

			if( flg & ENABLE_LIKELIHOOD_ONLY ) {
				std::cerr << boost::format("L= %+10.6e WG= %+10.6e logZ= %+10.6e") % L % WG % log(Z) << std::endl;
			}

			if( grad ) {

				auto Gms = computeGm(Z);
				auto idL = dL.begin();
				auto iw = weights->begin();
				for( int k = 0; k < dim; k++, idL++, iw++ ) {
					(*idL) += Gs[k] - Gms[k];

					if( !(flg & DISABLE_REGULARIZATION) ) {
						double dw2 = 2.0 * rp * (*iw);
						(*idL) -= dw2;
					}

					Logger::debug() << "dL(" << k << ")=" << *idL;
				}
			}
			Logger::debug() << "cache_hit_rate=" << (double)hit/(double)(miss+hit);
		}
	}

	std::vector<double> Learner::computeG(double& WG)
	{
		std::vector<double> Gs;
		auto segments = current_data->getSegments();
		assert( 0 < segments->size() );

		Gs.resize(dim);
		auto si = segments->begin();
		auto y1 = App::ZERO;
		for( ; si != segments->end(); si++ ){

			auto y = (*si)->getLabel();
			int ti = (*si)->getStart();
			int ui = (*si)->getEnd();
			WG += computeWG(y, y1, ui, ui-ti+1, gs);
			int k = 0;
			for( auto& g : Gs ) {
				g += gs(k++);
			}
			y1 = y;
		}

		int k = 0;
		for( auto g : Gs ) {
			Logger::debug() << "G(" << k++ << ")=" << g;
		}

		if( flg & ENABLE_LIKELIHOOD_ONLY ) {

			auto si = segments->begin();
			double awg = 0.0;

			auto y1 = App::ZERO;
			for( ; si != segments->end(); si++ ){

				double wg = 0.0;
				auto y = (*si)->getLabel();
				int ti = (*si)->getStart();
				int ui = (*si)->getEnd();
				wg = computeWG(y, y1, ui, ui-ti+1, gs);

				y1 = y;
				awg += wg;

				std::cerr << boost::format( "(%3d,%3d)" ) % ti % ui;
				std::cerr << boost::format(" WG= %+10.6e AWG= %+10.6e") % wg % awg << std::endl;
			}
		}

		return(std::move(Gs));
	}

	double Learner::computeZ()
	{
		double Z = 0;

		int l = labels->size();
		int s = current_data->getStrs()->size();
		int capacity = l*s;
		current_actab = createCheckTable(capacity);

		for( auto y : *labels ) {
			Z += alpha(s-1, y);
		}

		Logger::debug() << "Z=" << Z;
		return Z;
	}

	std::vector<double> Learner::computeGm(double Z)
	{
		std::vector<double> Gms;

		int l = labels->size();
		int s = current_data->getStrs()->size();
		int capacity = l*s;

		{
			uvector tmp(dim, 0.0);
			current_ecvtab = createCheckVTable(capacity);

			for( auto y : *labels ) {
				tmp += *eta(s-1, y);
			}

			tmp /= Z;
			for( int k = 0; k < dim; k++ ) {
				Gms.push_back(tmp(k));
				Logger::debug() << "Gm(" << k << ")=" << tmp(k);
			}
		}

		return(std::move(Gms));
	}

	double Learner::alpha(int i, Label y)
	{
		double v = 0;

		if( -1 < i ) {

			int idx = (i*labels->size()) + (static_cast<int>(y));
			auto& tp = current_actab->at(idx);

			if( std::get<0>(tp) ) {

				v = std::get<1>(tp);

			} else {

				for( int d = 1; d <= std::min(maxLength, i+1); d++ ) {
					for( auto yd : *labels ) {

						if( i == 0 && yd != App::ZERO ) {
							continue;
						}

						auto alp = alpha(i-d, yd);
						auto wg = computeWG(y, yd, i, d, gs);
						v += alp*exp(wg);
						if( std::isinf(v) || std::isnan(v) ) {
							throw Error("numerical problem");
						}
					}
				}

				std::get<0>(tp) = true;
				std::get<1>(tp) = v;
			}

		} else if( i == -1 ) {

			v = 1.0;

		} else {
			throw Error("fatal bug");
		}

		Logger::trace() << "alpha(i=" << i << ",y=" << (int)y << ")=" << v;
		return v;
	}

	double Learner::eta(int i, Label y, int k)
	{
		double v = 0;

		if( -1 < i ) {

			int idx = (i*labels->size()) + (static_cast<int>(y));
			auto& tp = current_ectab->at(idx);

			if( std::get<0>(tp) ) {

				v = std::get<1>(tp);

			} else {

				for( int d = 1; d <= std::min(maxLength, i+1); d++ ) {
					for( auto yd : *labels ) {

						if( i == 0 && yd != App::ZERO ) {
							continue;
						}

						double wg = computeWG(y, yd, i, d, gs);
						double gsk = gs(k); // alphaでもgsを使うので書き変わる前にすぐ保存する
						double cof = eta(i-d, yd, k) + alpha(i-d, yd) * gsk;
						v += cof*exp(wg);
						if( std::isinf(v) || std::isnan(v) ) {
							throw Error("numerical problem");
						}
					}
				}

				std::get<0>(tp) = true;
				std::get<1>(tp) = v;
			}

		} else if( i == -1 ) {

			v = 0.0;

		} else {
			throw Error("fatal bug");
		}

		Logger::trace() << "eta(i=" << i << ",y=" << (int)y << ",k=" << k << ")=" << v;
		return v;
	}

	SVector Learner::eta(int i, Label y)
	{
		SVector sv;

		if( -1 < i ) {

			int idx = (i*labels->size()) + (static_cast<int>(y));
			auto& tp = current_ecvtab->at(idx);

			if( std::get<0>(tp) ) {

				sv = std::get<1>(tp);

			} else {

				sv = std::make_shared<uvector>(dim, 0.0);

				for( int d = 1; d <= std::min(maxLength, i+1); d++ ) {
					for( auto yd : *labels ) {

						if( i == 0 && yd != App::ZERO ) {
							continue;
						}

						uvector gs(dim, 0.0); // alphaでもgsを使うのでローカルで領域を確保
						auto wg = computeWG(y, yd, i, d, gs);
						uvector cof = (*eta(i-d, yd)) + alpha(i-d, yd) * gs;
						auto ex = exp(wg);
						if( std::isinf(ex) || std::isnan(ex) ) {
							throw Error("numerical problem");
						}
						(*sv) += cof*ex;
					}
				}

				std::get<0>(tp) = true;
				std::get<1>(tp) = sv;
			}

		} else if( i == -1 ) {

			sv = std::make_shared<uvector>(dim, 0.0);

		} else {
			throw Error("fatal bug");
		}

		Logger::trace() << "eta(i=" << i << ",y=" << (int)y << ")=" << *sv;
		return sv;
	}

	//// Likilihood ////

	Likelihood::Likelihood(Learner* arg)
		: learner(arg)
	{
	}

	Likelihood::~Likelihood()
	{
	}

	Optimizer::ObjectiveFunction createLikelihood(Learner* learner)
	{
	 	return std::make_shared<Likelihood>(learner);
	}

	double Likelihood::value(uvector& x)
	{
		int i = 0;
		for( auto& w : *(learner->weights) ) {
			w = x[i++];
		}

		L = 0.0;
		std::vector<double> dL(learner->dim);
		learner->computeGrad(L, dL, false);

		return (-L); //
	}

	double Likelihood::savedValue()
	{
		return (-L); //
	}

	uvector Likelihood::grad(uvector& x)
	{
		int i;

		i = 0;
		for( auto& w : *(learner->weights) ) {
			w = x[i++];
		}

		L = 0.0;
		std::vector<double> dL(learner->dim);
		learner->computeGrad(L, dL, true);

		uvector g(learner->dim);

		i = 0;
		for( auto& idL : dL ) {
			g(i++) = (-idL); //
		}

		return std::move(g);
	}

	void Likelihood::preProcess(uvector& x)
	{
		int i = 0;
		for( auto& w : *(learner->weights) ) {
			x[i++] = w;
		}
	}

	void Likelihood::beginLoopProcess(uvector& x)
	{
	}

	void Likelihood::afterUpdateXProcess(uvector& x)
	{
	}

	void Likelihood::endLoopProcess(uvector& x)
	{
	}

	void Likelihood::postProcess(uvector& x)
	{
		int i = 0;
		for( auto& w : *(learner->weights) ) {
			w = x[i++];
		}
	}

	//// Predictor ////

	decltype( std::make_shared<Algorithm>() ) createPredictor(int arg)
	{
		return std::make_shared<Predictor>(arg);
	}

	Predictor::Predictor(int arg)
		: Algorithm(arg)
	{
		Logger::debug() << "Predictor()";
		Logger::info() << "Prediction ...";
	}

	Predictor::~Predictor()
	{
		Logger::debug() << "~Predictor()";
	}

	void Predictor::preProcess
	(   const std::string& wfile
	  , const std::string& w0file
	  , const std::string& w2vfile
		)
	{
		// 重みを生成しファイルから読み込む
		auto weights = SemiCrf::createWeights();
		std::ifstream ifs; // 入力
		open(ifs, wfile);
		try {
			weights->read(ifs);
		} catch(Error& e) {
			std::stringstream ss;
			ss << "failed to read " << wfile << ": " << e.what();
			throw Error(ss.str());
		}
		setWeights(weights);

		// maxLengthが明示的に指定されていなければ自動設定する
		if( maxLength < 1 ) {
			int ml = weights->getMaxLength();
			if( 0 < ml ) {
				setMaxLength(ml);
			} else {
				throw Error("negative maxLength specifed in weight file");
			}
		}

		// weightsからx,yの次元、featureを取得する
		int xdim = weights->getXDim();
		int ydim = weights->getYDim();
		const auto& feature = weights->getFeature();
		const auto& mean = weights->getMean();
		const auto& variance = weights->getVariance();

		// feature関数を生成し、x,yの次元、feature、maxLengthを設定する
		ff = App::createFeatureFunction(feature, w2vfile);
		ff->setXDim(xdim);
		ff->setYDim(ydim);
		ff->setMaxLength(maxLength);

		// featureから次元を取得しアルゴリズムに設定
		dim = ff->getDim();
		if( weights->size() != dim ) {
			throw Error("dimension mismatch between feature function and weight file");
		}

		// datasにx,yの次元、feature, mean, varianceを設定する
		datas->setXDim(xdim);
		datas->setYDim(ydim);
		if( datas->getFeature() != feature ) {
			// 推論データのfeatureが重みファイルと整合していない、推論データには明示的にfeatuteを指定する必要はない
			throw Error("feature mismatch between data file and weight file");
		}
		datas->setFeature(feature);
		datas->setMean(mean);
		datas->setVariance(variance);

		// ラベルを生成
		auto labels = createLabels(ydim);
		setLabels(labels);

		// 作業領域を初期化
		gs.resize(dim);
	}

	void Predictor::postProcess(const std::string& wfile)
	{
		datas->write(std::cout);
	}

	void Predictor::compute()
	{
		Logger::debug() << "Predictor::compute()";

		for( auto data : *datas ) {

			current_data = data;

			int l = labels->size();
			int s = current_data->getStrs()->size();
			int capacity = l*s;

			current_vctab = createCheckTable(capacity);
			current_wgtab = createCacheTable(cacheSize);

			int maxd = - 1;
			Label maxy;
			auto maxV = - std::numeric_limits<double>::max();

			for( auto y : *labels ) {

				int d = -1;
				auto v = V(s-1, y, d);

				if( maxV < v ) {
					maxy = y;
					maxV = v;
					maxd = d;
				}
			}

			if( flg & ENABLE_LIKELIHOOD_ONLY ) {
				std::cerr << boost::format("WG(maxV)= %10.6e") % maxV << std::endl;
			}
			assert( 0 < maxd );
			backtrack(maxy, maxd);
			printV();
		}
	}

	double Predictor::V(int i, Label y, int& maxd)
	{
		auto maxV = - std::numeric_limits<double>::max();

		if( -1 < i ) {

			int idx = (i*labels->size()) + (static_cast<int>(y));
			auto& tp = current_vctab->at(idx);

			if( std::get<0>(tp) ) {

				maxd = std::get<2>(tp);
				maxV = std::get<1>(tp);

			} else {

				maxd = -1;
				Label maxyd;

				for( int d = 1; d <= std::min(maxLength, i+1); d++ ) {
					for( auto yd : *labels ) {

						if( i == 0 && yd != App::ZERO ) {
							continue;
						}

						int tmp = -1;
						auto v = V(i-d, yd, tmp);
						v += computeWG(y, yd, i, d, gs);
					
						if( maxV < v ) {
							maxV = v;
							maxd = d;
							maxyd = yd;
						}
					}
				}

				assert( 0 < maxd );
				std::get<0>(tp) = true;
				std::get<1>(tp) = maxV;
				std::get<2>(tp) = maxd;
				std::get<3>(tp) = maxyd;
			}

		} else if( i == -1 ) {

			maxV = 0.0;

		} else {
			throw Error("fatal bug");
		}

		Logger::trace() << "V(i=" << i << ", y=" << (int)y << ")=" << maxV;
		return maxV;
	}

	void Predictor::backtrack(Label maxy, int maxd)
	{
		Logger::trace() << "Predictor::backtrack()";

		int l = labels->size();
		int s = current_data->getStrs()->size();
		int i = s-1;
		int idx = i*l + (int)maxy;
		auto& tp0 = current_vctab->at(idx);
		maxd = std::get<2>(tp0);
		auto maxyd = std::get<3>(tp0);

		std::list<decltype(std::make_shared<Segment>())> ls;

		while(1) {

			auto seg = createSegment(i-maxd+1, i, maxy);
			ls.push_front(seg);

			i -= maxd;
			if( i < 0 ) {
				assert( i == -1 );
				break;
			}

			idx = i*l + (int)maxyd;
			auto& tp = current_vctab->at(idx);
			maxy = maxyd;
			maxd = std::get<2>(tp);
			maxyd = std::get<3>(tp);
		}

		for( auto s : ls ) {
			current_data->getSegments()->push_back(s);
		}
	}

	void Predictor::printV()
	{
		if( flg & ENABLE_LIKELIHOOD_ONLY ) {

			int l = labels->size();
			int s = current_data->getStrs()->size();
			for( int i = 0; i < s; i++ ) {
				for( auto y : *labels ) {

					int idx = i*l + (static_cast<int>(y));
					auto& tp = current_vctab->at(idx);
					auto maxV = std::get<1>(tp);
					int maxd = std::get<2>(tp);
					auto maxyd = std::get<3>(tp);
					std::cerr << boost::format("(%+10.6e %2d %2d)") % maxV % maxd % (int)maxyd << " " << std::endl;
				}
			}
		}
	}
}
