#include "model.h"
#include "instance.h"

using namespace std;

class BinaryFile
{
	private:
		vector<Model*> models;
		vector<Armature*> armatures;
		
	public:
		int instances = 0;

		void AddAnimation(const char *file, bool verbose);
		vector<int> GetCountFramesInAnimations();
		Instance* CreateInstance(dmGameObject::HInstance obj, bool useBakedAnimations, float scaleAABB);

		BinaryFile(const char* file, bool verbose);
		~BinaryFile();
};