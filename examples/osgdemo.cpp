/* OpenSceneGraph example, osgwindows.
*
*  Permission is hereby granted, free of charge, to any person obtaining a copy
*  of this software and associated documentation files (the "Software"), to deal
*  in the Software without restriction, including without limitation the rights
*  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
*  copies of the Software, and to permit persons to whom the Software is
*  furnished to do so, subject to the following conditions:
*
*  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
*  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
*  THE SOFTWARE.
*/
#include <osg/ShapeDrawable>

#include <osgDB/ReadFile>
#include <osgUtil/Optimizer>
#include <osgViewer/Viewer>

//USE_OSGPLUGIN(freetype)

//USE_DOTOSGWRAPPER_LIBRARY(osg)
//USE_SERIALIZER_WRAPPER_LIBRARY(osg)

USE_GRAPHICSWINDOW()

osg::ref_ptr<osg::Geode> CreateShape()
{
	float radius = 0.8f;
	float height = 1.2f;

	osg::ref_ptr<osg::TessellationHints> hints = new osg::TessellationHints;
	hints->setDetailRatio(0.5f);


	osg::ref_ptr<osg::Geode> geode = new osg::Geode();

	//球体
	geode->addDrawable(new osg::ShapeDrawable(new osg::Sphere(osg::Vec3(0.0f,0.0f,0.0f),  radius)));

	//立方体
	geode->addDrawable(new osg::ShapeDrawable(new osg::Box(osg::Vec3(2.0f,0.0f,0.0f), 2*radius)));

	//圆锥
	geode->addDrawable(new osg::ShapeDrawable(new osg::Cone(osg::Vec3(4.0f,0.0f,0.0f), radius, height), hints));

	//圆柱体
	geode->addDrawable(new osg::ShapeDrawable(new osg::Cylinder(osg::Vec3(6.0f,0.0f,0.0f), radius, height), hints));

	//胶囊体
	geode->addDrawable(new osg::ShapeDrawable(new osg::Capsule(osg::Vec3(8.0f,0.0f,0.0f), radius, height), hints));

	return geode.get();
}


int main( int argc, char **argv )
{
	osg::ref_ptr<osgViewer::Viewer> viewer = new osgViewer::Viewer();

	osg::ref_ptr<osg::Group> root = new osg::Group();

	root->addChild(CreateShape());

	viewer->setSceneData(root.get());

    return viewer->run();
}

