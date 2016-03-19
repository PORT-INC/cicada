// © 2016 PORT INC.

#ifndef APP_TEST__H
#define APP_TEST__H

#include "SemiCrf.hpp"
#include "DebugOut.hpp"

namespace App {

	enum class Label : int {
		ZERO = 0,
		ONE = 1
	};

	class Simple : public SemiCrf::FeatureFunction_ {
	public:

		Simple(){ Debug::out(2) << "Simple()" << std::endl; };
		virtual ~Simple() { Debug::out(1) << "~Simple()" << std::endl; };

		virtual void read() {
			Debug::out(2) << "Simple::read()" << std::endl;
		};

		virtual void write() {
			Debug::out(2) << "Simple::write()" << std::endl;
		};

		virtual double operator() (int k, Label y, Label yd, SemiCrf::Data x, int j, int i);
	};

	SemiCrf::FeatureFunction createFeatureFunction();
	int getFeatureDimention();

	SemiCrf::Labels createLabels();
}

#endif // APP_TEST__H
