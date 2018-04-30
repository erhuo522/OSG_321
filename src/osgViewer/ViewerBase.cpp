/* -*-c++-*- OpenSceneGraph - Copyright (C) 1998-2006 Robert Osfield
 *
 * This library is open source and may be redistributed and/or modified under
 * the terms of the OpenSceneGraph Public License (OSGPL) version 0.0 or
 * (at your option) any later version.  The full license is in LICENSE file
 * included with this distribution, and on the openscenegraph.org website.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * OpenSceneGraph Public License for more details.
*/

#include <stdlib.h>
#include <string.h>

#include <osgViewer/ViewerBase>
#include <osgViewer/View>
#include <osgViewer/Renderer>

#include <osg/io_utils>

#include <osg/TextureCubeMap>
#include <osg/TextureRectangle>
#include <osg/TexMat>
#include <osg/DeleteHandler>

#include <osgUtil/Optimizer>
#include <osgUtil/IntersectionVisitor>
#include <osgUtil/Statistics>

static osg::ApplicationUsageProxy ViewerBase_e0(osg::ApplicationUsage::ENVIRONMENTAL_VARIABLE,"OSG_CONFIG_FILE <filename>","Specify a viewer configuration file to load by default.");
static osg::ApplicationUsageProxy ViewerBase_e1(osg::ApplicationUsage::ENVIRONMENTAL_VARIABLE,"OSG_THREADING <value>","Set the threading model using by Viewer, <value> can be SingleThreaded, CullDrawThreadPerContext, DrawThreadPerContext or CullThreadPerCameraDrawThreadPerContext.");
static osg::ApplicationUsageProxy ViewerBase_e2(osg::ApplicationUsage::ENVIRONMENTAL_VARIABLE,"OSG_SCREEN <value>","Set the default screen that windows should open up on.");
static osg::ApplicationUsageProxy ViewerBase_e3(osg::ApplicationUsage::ENVIRONMENTAL_VARIABLE,"OSG_WINDOW x y width height","Set the default window dimensions that windows should open up on.");
static osg::ApplicationUsageProxy ViewerBase_e4(osg::ApplicationUsage::ENVIRONMENTAL_VARIABLE,"OSG_RUN_FRAME_SCHEME","Frame rate manage scheme that viewer run should use,  ON_DEMAND or CONTINUOUS (default).");
static osg::ApplicationUsageProxy ViewerBase_e5(osg::ApplicationUsage::ENVIRONMENTAL_VARIABLE,"OSG_RUN_MAX_FRAME_RATE","Set the maximum number of frame as second that viewer run. 0.0 is default and disables an frame rate capping.");

using namespace osgViewer;

ViewerBase::ViewerBase():
    osg::Object(true)
{
    viewerBaseInit();
}

ViewerBase::ViewerBase(const ViewerBase&):
    osg::Object(true)
{
    viewerBaseInit();
}

void ViewerBase::viewerBaseInit()
{
    _firstFrame = true;
    _done = false;
    _keyEventSetsDone = osgGA::GUIEventAdapter::KEY_Escape;
    _quitEventSetsDone = true;
    _releaseContextAtEndOfFrameHint = true;
    _threadingModel = AutomaticSelection;
    _threadsRunning = false;
    _endBarrierPosition = AfterSwapBuffers;
    _endBarrierOperation = osg::BarrierOperation::NO_OPERATION;
    _requestRedraw = true;
    _requestContinousUpdate = false;

    _runFrameScheme = CONTINUOUS;
    _runMaxFrameRate = 0.0f;

    const char* str = getenv("OSG_RUN_FRAME_SCHEME");
    if (str)
    {
        if      (strcmp(str, "ON_DEMAND")==0) _runFrameScheme = ON_DEMAND;
        else if (strcmp(str, "CONTINUOUS")==0) _runFrameScheme = CONTINUOUS;
    }

    str = getenv("OSG_RUN_MAX_FRAME_RATE");
    if (str)
    {
        _runMaxFrameRate = osg::asciiToDouble(str);
    }
}

void ViewerBase::setThreadingModel(ThreadingModel threadingModel)
{
    if (_threadingModel == threadingModel) return;

    if (_threadsRunning) stopThreading();

    _threadingModel = threadingModel;

    if (isRealized() && _threadingModel!=SingleThreaded) startThreading();
}

ViewerBase::ThreadingModel ViewerBase::suggestBestThreadingModel()
{
    const char* str = getenv("OSG_THREADING");
    if (str)
    {
        if (strcmp(str,"SingleThreaded")==0) return SingleThreaded;
        else if (strcmp(str,"CullDrawThreadPerContext")==0) return CullDrawThreadPerContext;
        else if (strcmp(str,"DrawThreadPerContext")==0) return DrawThreadPerContext;
        else if (strcmp(str,"CullThreadPerCameraDrawThreadPerContext")==0) return CullThreadPerCameraDrawThreadPerContext;
    }

    Contexts contexts;
    getContexts(contexts);

    if (contexts.empty()) return SingleThreaded;

#if 0
    // temporary hack to disable multi-threading under Windows till we find good solutions for
    // crashes that users are seeing.
    return SingleThreaded;
#endif

    Cameras cameras;
    getCameras(cameras);

    if (cameras.empty()) return SingleThreaded;


    int numProcessors = OpenThreads::GetNumberOfProcessors();

    if (contexts.size()==1)
    {
        if (numProcessors==1) return SingleThreaded;
        else return DrawThreadPerContext;
    }

#if 1
    if (numProcessors >= static_cast<int>(cameras.size()+contexts.size()))
    {
        return CullThreadPerCameraDrawThreadPerContext;
    }
#endif

    return DrawThreadPerContext;
}

void ViewerBase::setUpThreading()
{
    Contexts contexts;
    getContexts(contexts);

    if (_threadingModel==SingleThreaded)
    {
        if (_threadsRunning) stopThreading();
        else
        {
            // we'll set processor affinity here to help single threaded apps
            // with multiple processor cores, and using the database pager.
            int numProcessors = OpenThreads::GetNumberOfProcessors();
            bool affinity = numProcessors>1;
            if (affinity)
            {
                OpenThreads::SetProcessorAffinityOfCurrentThread(0);

                Scenes scenes;
                getScenes(scenes);
            }
        }
    }
    else
    {
        if (!_threadsRunning) startThreading();
    }

}

void ViewerBase::setEndBarrierPosition(BarrierPosition bp)
{
    if (_endBarrierPosition == bp) return;

    if (_threadsRunning) stopThreading();

    _endBarrierPosition = bp;

    if (_threadingModel!=SingleThreaded) startThreading();
}

void ViewerBase::setEndBarrierOperation(osg::BarrierOperation::PreBlockOp op)
{
    if (_endBarrierOperation == op) return;

    if (_threadsRunning) stopThreading();

    _endBarrierOperation = op;

    if (_threadingModel!=SingleThreaded) startThreading();
}

void ViewerBase::stopThreading()
{
    if (!_threadsRunning) return;

    OSG_INFO<<"ViewerBase::stopThreading() - stopping threading"<<std::endl;

    Contexts contexts;
    getContexts(contexts);

    Cameras cameras;
    getCameras(cameras);

    Contexts::iterator gcitr;
    Cameras::iterator citr;

	//先释放渲染线程的阻塞
    for(Cameras::iterator camItr = cameras.begin();
        camItr != cameras.end();
        ++camItr)
    {
        osg::Camera* camera = *camItr;
        Renderer* renderer = dynamic_cast<Renderer*>(camera->getRenderer());
        if (renderer) renderer->release();
    }

    // 删除所有渲染线程
    for(gcitr = contexts.begin();
        gcitr != contexts.end();
        ++gcitr)
    {
        (*gcitr)->setGraphicsThread(0);
    }

    // 删除所有相机线程
    for(citr = cameras.begin();
        citr != cameras.end();
        ++citr)
    {
        (*citr)->setCameraThread(0);
    }

    for(Cameras::iterator camItr = cameras.begin();
        camItr != cameras.end();
        ++camItr)
    {
        osg::Camera* camera = *camItr;
        Renderer* renderer = dynamic_cast<Renderer*>(camera->getRenderer());
        if (renderer)
        {
            renderer->setGraphicsThreadDoesCull( true );
            renderer->setDone(false);
        }
    }


    _threadsRunning = false;
    _startRenderingBarrier = 0;
    _endRenderingDispatchBarrier = 0;
    _endDynamicDrawBlock = 0;

    OSG_INFO<<"Viewer::stopThreading() - stopped threading."<<std::endl;
}
/**  一个GraphicsThread线程可以渲染出多个Camera相机对应的画面；
  *  GraphicsThread进行渲染时，需要针对每个相机，进行一次数据裁剪；
  *  为了提高效率，osg支持每个相机创建一个自己的CameraThread线程，用来做裁剪工作。
  */
void ViewerBase::startThreading()
{
    if (_threadsRunning) return;

    OSG_INFO<<"Viewer::startThreading() - starting threading"<<std::endl;

    // release any context held by the main thread.
    releaseContext();

	//获取线程模型
    _threadingModel = _threadingModel==AutomaticSelection ? suggestBestThreadingModel() : _threadingModel;

	//获取所有context
    Contexts contexts;
    getContexts(contexts);

    OSG_INFO<<"Viewer::startThreading() - contexts.size()="<<contexts.size()<<std::endl;

	//获取所有相机
    Cameras cameras;
    getCameras(cameras);

    unsigned int numThreadsOnStartBarrier = 0;
    unsigned int numThreadsOnEndBarrier = 0;
    switch(_threadingModel)
    {
        case(SingleThreaded):
            numThreadsOnStartBarrier = 1;
            numThreadsOnEndBarrier = 1;
            return;
        case(CullDrawThreadPerContext):
            numThreadsOnStartBarrier = contexts.size()+1; //context线程数量+主线程
            numThreadsOnEndBarrier = contexts.size()+1;
            break;
        case(DrawThreadPerContext):
            numThreadsOnStartBarrier = 1;
            numThreadsOnEndBarrier = 1;
            break;
        case(CullThreadPerCameraDrawThreadPerContext):
            numThreadsOnStartBarrier = cameras.size()+1;
            numThreadsOnEndBarrier = 1;
            break;
        default:
            OSG_NOTICE<<"Error: Threading model not selected"<<std::endl;
            return;
    }

    // using multi-threading so make sure that new objects are allocated with thread safe ref/unref
	//设置线程安全模式计数器
    osg::Referenced::setThreadSafeReferenceCounting(true);

    Scenes scenes;
    getScenes(scenes);
    for(Scenes::iterator scitr = scenes.begin();
        scitr != scenes.end();
        ++scitr)
    {
        if ((*scitr)->getSceneData())
        {
            OSG_INFO<<"Making scene thread safe"<<std::endl;

            // make sure that existing scene graph objects are allocated with thread safe ref/unref
            (*scitr)->getSceneData()->setThreadSafeRefUnref(true);

            // update the scene graph so that it has enough GL object buffer memory for the graphics contexts that will be using it.
            (*scitr)->getSceneData()->resizeGLObjectBuffers(osg::DisplaySettings::instance()->getMaxNumberOfGraphicsContexts());
        }
    }

	//获取处理器数量
    int numProcessors = OpenThreads::GetNumberOfProcessors();
	//是否多核的CPU
    bool affinity = numProcessors>1;

    Contexts::iterator citr;

    unsigned int numViewerDoubleBufferedRenderingOperation = 0;

	//CullDrawThreadPerContext 和 SingleThreaded模式，裁剪和渲染共用一个线程
    bool graphicsThreadsDoesCull = _threadingModel == CullDrawThreadPerContext || _threadingModel==SingleThreaded;

    for(Cameras::iterator camItr = cameras.begin();
        camItr != cameras.end();
        ++camItr)
    {
        osg::Camera* camera = *camItr;
        Renderer* renderer = dynamic_cast<Renderer*>(camera->getRenderer());
        if (renderer)
        {
            renderer->setGraphicsThreadDoesCull(graphicsThreadsDoesCull);
            renderer->setDone(false);
            renderer->reset();
            ++numViewerDoubleBufferedRenderingOperation;
        }
    }

    if (_threadingModel==CullDrawThreadPerContext)
    {
        _startRenderingBarrier = 0;
        _endRenderingDispatchBarrier = 0;
        _endDynamicDrawBlock = 0;
    }
    else if (_threadingModel==DrawThreadPerContext ||
             _threadingModel==CullThreadPerCameraDrawThreadPerContext)
    {
        _startRenderingBarrier = 0;
        _endRenderingDispatchBarrier = 0;
        _endDynamicDrawBlock = new osg::EndOfDynamicDrawBlock(numViewerDoubleBufferedRenderingOperation);

#ifndef OSGUTIL_RENDERBACKEND_USE_REF_PTR
        if (!osg::Referenced::getDeleteHandler()) osg::Referenced::setDeleteHandler(new osg::DeleteHandler(2));
        else osg::Referenced::getDeleteHandler()->setNumFramesToRetainObjects(2);
#endif
    }

    if (numThreadsOnStartBarrier>1)
    {
		//开始渲染的线程同步栅栏
        _startRenderingBarrier = new osg::BarrierOperation(numThreadsOnStartBarrier, osg::BarrierOperation::NO_OPERATION);
    }

    if (numThreadsOnEndBarrier>1)
    {
		//渲染完成的线程同步栅栏
        _endRenderingDispatchBarrier = new osg::BarrierOperation(numThreadsOnEndBarrier, _endBarrierOperation);
    }

	//渲染前后台切换的线程同步栅栏
    osg::ref_ptr<osg::BarrierOperation> swapReadyBarrier = contexts.empty() ? 0 : new osg::BarrierOperation(contexts.size(), osg::BarrierOperation::NO_OPERATION);

	//swap操作对象
    osg::ref_ptr<osg::SwapBuffersOperation> swapOp = new osg::SwapBuffersOperation();

    typedef std::map<OpenThreads::Thread*, int> ThreadAffinityMap;
    ThreadAffinityMap threadAffinityMap;

	//根据context数量创建渲染线程
    unsigned int processNum = 1;
    for(citr = contexts.begin();
        citr != contexts.end();
        ++citr, ++processNum)
    {
        osg::GraphicsContext* gc = (*citr);

        if (!gc->isRealized())
        {
            OSG_INFO<<"ViewerBase::startThreading() : Realizng window "<<gc<<std::endl;
            gc->realize();
        }

        gc->getState()->setDynamicObjectRenderingCompletedCallback(_endDynamicDrawBlock.get());

        // create the a graphics thread for this context
		//创建渲染线程
        gc->createGraphicsThread();
		//如果是多核处理器，则设置CPU相关性
        if (affinity)
		{
				gc->getGraphicsThread()->setProcessorAffinity(processNum % numProcessors);
		}
        threadAffinityMap[gc->getGraphicsThread()] = processNum % numProcessors;

        // add the startRenderingBarrier
        if (_threadingModel==CullDrawThreadPerContext && _startRenderingBarrier.valid())
		{
				gc->getGraphicsThread()->add(_startRenderingBarrier.get());
		}
        // add the rendering operation itself.
		//添加渲染操作
        gc->getGraphicsThread()->add(new osg::RunOperations());

        if (_threadingModel==CullDrawThreadPerContext && _endBarrierPosition==BeforeSwapBuffers && _endRenderingDispatchBarrier.valid())
        {
            // add the endRenderingDispatchBarrier
            gc->getGraphicsThread()->add(_endRenderingDispatchBarrier.get());
        }
		//多个渲染线程同步栅栏，防止多个GraphicsThread渲染的画面不一致。
        if (swapReadyBarrier.valid()) 
		{
			gc->getGraphicsThread()->add(swapReadyBarrier.get());
		}

        // add the swap buffers
		//添加SwapBuffer操作
        gc->getGraphicsThread()->add(swapOp.get());

        if (_threadingModel==CullDrawThreadPerContext && _endBarrierPosition==AfterSwapBuffers && _endRenderingDispatchBarrier.valid())
        {
            // add the endRenderingDispatchBarrier
            gc->getGraphicsThread()->add(_endRenderingDispatchBarrier.get());
        }

    }

    if (_threadingModel==CullThreadPerCameraDrawThreadPerContext && numThreadsOnStartBarrier>1)
    {
        Cameras::iterator camItr;

        for(camItr = cameras.begin();
            camItr != cameras.end();
            ++camItr, ++processNum)
        {
            osg::Camera* camera = *camItr;
            camera->createCameraThread();
			
			//为线程分配处理器
            if (affinity) 
			{
				camera->getCameraThread()->setProcessorAffinity(processNum % numProcessors);
			}
			threadAffinityMap[camera->getCameraThread()] = processNum % numProcessors;

            osg::GraphicsContext* gc = camera->getGraphicsContext();

            // 添加startRenderingBarrier线程同步栅栏，所有相机裁剪完成后，GraphicsThread线程开始渲染
            if (_startRenderingBarrier.valid()) 
			{
					camera->getCameraThread()->add(_startRenderingBarrier.get());
			}
			//设置渲染线程不裁剪数据，
            Renderer* renderer = dynamic_cast<Renderer*>(camera->getRenderer());
            renderer->setGraphicsThreadDoesCull(false);

            camera->getCameraThread()->add(renderer);

            if (_endRenderingDispatchBarrier.valid())
            {
                // 添加endRenderingDispatchBarrier线程同步栅栏
                gc->getGraphicsThread()->add(_endRenderingDispatchBarrier.get());
            }

        }
		//启动相机的裁剪线程
        for(camItr = cameras.begin();
            camItr != cameras.end();
            ++camItr)
        {
            osg::Camera* camera = *camItr;
            if (camera->getCameraThread() && !camera->getCameraThread()->isRunning())
            {
                OSG_INFO<<"  camera->getCameraThread()-> "<<camera->getCameraThread()<<std::endl;
                camera->getCameraThread()->startThread();
            }
        }
    }

#if 0
    if (affinity)
    {
        OpenThreads::SetProcessorAffinityOfCurrentThread(0);
        if (_scene.valid() && _scene->getDatabasePager())
        {
#if 0
            _scene->getDatabasePager()->setProcessorAffinity(1);
#else
            _scene->getDatabasePager()->setProcessorAffinity(0);
#endif
        }
    }
#endif

#if 0
    if (affinity)
    {
        for(ThreadAffinityMap::iterator titr = threadAffinityMap.begin();
            titr != threadAffinityMap.end();
            ++titr)
        {
            titr->first->setProcessorAffinity(titr->second);
        }
    }
#endif

	//启动GraphicsThread裁剪线程
    for(citr = contexts.begin();
        citr != contexts.end();
        ++citr)
    {
        osg::GraphicsContext* gc = (*citr);
        if (gc->getGraphicsThread() && !gc->getGraphicsThread()->isRunning())
        {
            OSG_INFO<<"  gc->getGraphicsThread()->startThread() "<<gc->getGraphicsThread()<<std::endl;
            gc->getGraphicsThread()->startThread();
            // OpenThreads::Thread::YieldCurrentThread();
        }
    }

    _threadsRunning = true;

    OSG_INFO<<"Set up threading"<<std::endl;
}

void ViewerBase::getWindows(Windows& windows, bool onlyValid)
{
    windows.clear();

    Contexts contexts;
    getContexts(contexts, onlyValid);

    for(Contexts::iterator itr = contexts.begin();
        itr != contexts.end();
        ++itr)
    {
        osgViewer::GraphicsWindow* gw = dynamic_cast<osgViewer::GraphicsWindow*>(*itr);
        if (gw) windows.push_back(gw);
    }
}

void ViewerBase::checkWindowStatus()
{
    Contexts contexts;
    getContexts(contexts);
    checkWindowStatus(contexts);
}

void ViewerBase::checkWindowStatus(const Contexts& contexts)
{
    if (contexts.size()==0)
    {
        _done = true;
        if (areThreadsRunning()) stopThreading();
    }
}

void ViewerBase::addUpdateOperation(osg::Operation* operation)
{
    if (!operation) return;

    if (!_updateOperations) _updateOperations = new osg::OperationQueue;

    _updateOperations->add(operation);
}

void ViewerBase::removeUpdateOperation(osg::Operation* operation)
{
    if (!operation) return;

    if (_updateOperations.valid())
    {
        _updateOperations->remove(operation);
    }
}

void ViewerBase::setIncrementalCompileOperation(osgUtil::IncrementalCompileOperation* ico)
{
    if (_incrementalCompileOperation == ico) return;

    Contexts contexts;
    getContexts(contexts, false);

    if (_incrementalCompileOperation.valid()) _incrementalCompileOperation->removeContexts(contexts);

    // assign new operation
    _incrementalCompileOperation = ico;

    Scenes scenes;
    getScenes(scenes,false);
    for(Scenes::iterator itr = scenes.begin();
        itr != scenes.end();
        ++itr)
    {
        osgDB::DatabasePager* dp = (*itr)->getDatabasePager();
        dp->setIncrementalCompileOperation(ico);
    }


    if (_incrementalCompileOperation) _incrementalCompileOperation->assignContexts(contexts);
}
//主线程的run循环
int ViewerBase::run()
{
    if (!isRealized())
    {
        realize();
    }

    const char* run_frame_count_str = getenv("OSG_RUN_FRAME_COUNT");
    unsigned int runTillFrameNumber = run_frame_count_str==0 ? osg::UNINITIALIZED_FRAME_NUMBER : atoi(run_frame_count_str);

    while(!done() && (run_frame_count_str==0 || getViewerFrameStamp()->getFrameNumber()<runTillFrameNumber))
    {
        double minFrameTime = _runMaxFrameRate>0.0 ? 1.0/_runMaxFrameRate : 0.0;
        osg::Timer_t startFrameTick = osg::Timer::instance()->tick();
        if (_runFrameScheme==ON_DEMAND)
        {
            if (checkNeedToDoFrame())
            {
                frame();
            }
            else
            {
                // we don't need to render a frame but we don't want to spin the run loop so make sure the minimum
                // loop time is 1/100th of second, if not otherwise set, so enabling the frame microSleep below to
                // avoid consume excessive CPU resources.
                if (minFrameTime==0.0) minFrameTime=0.01;
            }
        }
        else
        {
            frame();
        }

        // work out if we need to force a sleep to hold back the frame rate
        osg::Timer_t endFrameTick = osg::Timer::instance()->tick();
        double frameTime = osg::Timer::instance()->delta_s(startFrameTick, endFrameTick);
        if (frameTime < minFrameTime) OpenThreads::Thread::microSleep(static_cast<unsigned int>(1000000.0*(minFrameTime-frameTime)));
    }

    return 0;
}

void ViewerBase::frame(double simulationTime)
{
    if (_done) return;

    // OSG_NOTICE<<std::endl<<"CompositeViewer::frame()"<<std::endl<<std::endl;

    if (_firstFrame)
    {
        viewerInit();

        if (!isRealized())
        {
            realize();
        }

        _firstFrame = false;
    }
    advance(simulationTime);

    eventTraversal();
    updateTraversal();
    renderingTraversals();
}

//完成数据的裁剪和场景渲染工作
void ViewerBase::renderingTraversals()
{
    bool _outputMasterCameraLocation = false;
    if (_outputMasterCameraLocation)
    {
        Views views;
        getViews(views);

        for(Views::iterator itr = views.begin();
            itr != views.end();
            ++itr)
        {
            osgViewer::View* view = *itr;
            if (view)
            {
                const osg::Matrixd& m = view->getCamera()->getInverseViewMatrix();
                OSG_NOTICE<<"View "<<view<<", Master Camera position("<<m.getTrans()<<"), rotation("<<m.getRotate()<<")"<<std::endl;
            }
        }
    }

    Contexts contexts;
    getContexts(contexts);

    // check to see if windows are still valid
    checkWindowStatus(contexts);
    if (_done) return;

    double beginRenderingTraversals = elapsedTime();

    osg::FrameStamp* frameStamp = getViewerFrameStamp();

    if (getViewerStats() && getViewerStats()->collectStats("scene"))
    {
        unsigned int frameNumber = frameStamp ? frameStamp->getFrameNumber() : 0;

        Views views;
        getViews(views);
        for(Views::iterator vitr = views.begin();
            vitr != views.end();
            ++vitr)
        {
            View* view = *vitr;
            osg::Stats* stats = view->getStats();
            osg::Node* sceneRoot = view->getSceneData();
            if (sceneRoot && stats)
            {
                osgUtil::StatsVisitor statsVisitor;
                sceneRoot->accept(statsVisitor);
                statsVisitor.totalUpStats();

                unsigned int unique_primitives = 0;
                osgUtil::Statistics::PrimitiveCountMap::iterator pcmitr;
                for(pcmitr = statsVisitor._uniqueStats.GetPrimitivesBegin();
                    pcmitr != statsVisitor._uniqueStats.GetPrimitivesEnd();
                    ++pcmitr)
                {
                    unique_primitives += pcmitr->second;
                }

                stats->setAttribute(frameNumber, "Number of unique StateSet", static_cast<double>(statsVisitor._statesetSet.size()));
                stats->setAttribute(frameNumber, "Number of unique Group", static_cast<double>(statsVisitor._groupSet.size()));
                stats->setAttribute(frameNumber, "Number of unique Transform", static_cast<double>(statsVisitor._transformSet.size()));
                stats->setAttribute(frameNumber, "Number of unique LOD", static_cast<double>(statsVisitor._lodSet.size()));
                stats->setAttribute(frameNumber, "Number of unique Switch", static_cast<double>(statsVisitor._switchSet.size()));
                stats->setAttribute(frameNumber, "Number of unique Geode", static_cast<double>(statsVisitor._geodeSet.size()));
                stats->setAttribute(frameNumber, "Number of unique Drawable", static_cast<double>(statsVisitor._drawableSet.size()));
                stats->setAttribute(frameNumber, "Number of unique Geometry", static_cast<double>(statsVisitor._geometrySet.size()));
                stats->setAttribute(frameNumber, "Number of unique Vertices", static_cast<double>(statsVisitor._uniqueStats._vertexCount));
                stats->setAttribute(frameNumber, "Number of unique Primitives", static_cast<double>(unique_primitives));

                unsigned int instanced_primitives = 0;
                for(pcmitr = statsVisitor._instancedStats.GetPrimitivesBegin();
                    pcmitr != statsVisitor._instancedStats.GetPrimitivesEnd();
                    ++pcmitr)
                {
                    instanced_primitives += pcmitr->second;
                }

                stats->setAttribute(frameNumber, "Number of instanced Stateset", static_cast<double>(statsVisitor._numInstancedStateSet));
                stats->setAttribute(frameNumber, "Number of instanced Group", static_cast<double>(statsVisitor._numInstancedGroup));
                stats->setAttribute(frameNumber, "Number of instanced Transform", static_cast<double>(statsVisitor._numInstancedTransform));
                stats->setAttribute(frameNumber, "Number of instanced LOD", static_cast<double>(statsVisitor._numInstancedLOD));
                stats->setAttribute(frameNumber, "Number of instanced Switch", static_cast<double>(statsVisitor._numInstancedSwitch));
                stats->setAttribute(frameNumber, "Number of instanced Geode", static_cast<double>(statsVisitor._numInstancedGeode));
                stats->setAttribute(frameNumber, "Number of instanced Drawable", static_cast<double>(statsVisitor._numInstancedDrawable));
                stats->setAttribute(frameNumber, "Number of instanced Geometry", static_cast<double>(statsVisitor._numInstancedGeometry));
                stats->setAttribute(frameNumber, "Number of instanced Vertices", static_cast<double>(statsVisitor._instancedStats._vertexCount));
                stats->setAttribute(frameNumber, "Number of instanced Primitives", static_cast<double>(instanced_primitives));
           }
        }
    }

    Scenes scenes;
    getScenes(scenes);

    for(Scenes::iterator sitr = scenes.begin();
        sitr != scenes.end();
        ++sitr)
    {
        Scene* scene = *sitr;
        osgDB::DatabasePager* dp = scene ? scene->getDatabasePager() : 0;
        if (dp) dp->signalBeginFrame(frameStamp);

        osgDB::ImagePager* ip = scene ? scene->getImagePager() : 0;
        if (ip) ip->signalBeginFrame(frameStamp);

        if (scene->getSceneData())
        {
            // fire off a build of the bounding volumes while we
            // are still running single threaded.
            scene->getSceneData()->getBound();
        }
    }

    // OSG_NOTICE<<std::endl<<"Start frame"<<std::endl;


    Cameras cameras;
    getCameras(cameras);

    Contexts::iterator itr;

    bool doneMakeCurrentInThisThread = false;

    if (_endDynamicDrawBlock.valid())
    {
        _endDynamicDrawBlock->reset();
    }

    // dispatch the rendering threads
    if (_startRenderingBarrier.valid()) _startRenderingBarrier->block();

    // 主线程执行裁剪工作
    for(Cameras::iterator camItr = cameras.begin();
        camItr != cameras.end();
        ++camItr)
    {
        osg::Camera* camera = *camItr;
        Renderer* renderer = dynamic_cast<Renderer*>(camera->getRenderer());
        if (renderer)
        {
            if (!renderer->getGraphicsThreadDoesCull() && !(camera->getCameraThread()))
            {
                renderer->cull();
            }
        }
    }


	//主线程执行渲染操作
    for(itr = contexts.begin();
        itr != contexts.end() && !_done;
        ++itr)
    {
        if (!((*itr)->getGraphicsThread()) && (*itr)->valid())
        {
            doneMakeCurrentInThisThread = true;
            makeCurrent(*itr);
            (*itr)->runOperations();
        }
    }

    // OSG_NOTICE<<"Joing _endRenderingDispatchBarrier block "<<_endRenderingDispatchBarrier.get()<<std::endl;

    // wait till the rendering dispatch is done.
    if (_endRenderingDispatchBarrier.valid()) _endRenderingDispatchBarrier->block();

	//主线程执行swap操作
    for(itr = contexts.begin();
        itr != contexts.end() && !_done;
        ++itr)
    {
        if (!((*itr)->getGraphicsThread()) && (*itr)->valid())
        {
            doneMakeCurrentInThisThread = true;
            makeCurrent(*itr);
            (*itr)->swapBuffers();
        }
    }

    for(Scenes::iterator sitr = scenes.begin();
        sitr != scenes.end();
        ++sitr)
    {
        Scene* scene = *sitr;
        osgDB::DatabasePager* dp = scene ? scene->getDatabasePager() : 0;
        if (dp) dp->signalEndFrame();

        osgDB::ImagePager* ip = scene ? scene->getImagePager() : 0;
        if (ip) ip->signalEndFrame();
    }

    // wait till the dynamic draw is complete.
    if (_endDynamicDrawBlock.valid())
    {
        // osg::Timer_t startTick = osg::Timer::instance()->tick();
        _endDynamicDrawBlock->block();
        // OSG_NOTICE<<"Time waiting "<<osg::Timer::instance()->delta_m(startTick, osg::Timer::instance()->tick())<<std::endl;;
    }

    if (_releaseContextAtEndOfFrameHint && doneMakeCurrentInThisThread)
    {
        //OSG_NOTICE<<"Doing release context"<<std::endl;
        releaseContext();
    }
	
	//打印相关的时间log信息
    if (getViewerStats() && getViewerStats()->collectStats("update"))
    {
        double endRenderingTraversals = elapsedTime();

        // update current frames stats
        getViewerStats()->setAttribute(frameStamp->getFrameNumber(), "Rendering traversals begin time ", beginRenderingTraversals);
        getViewerStats()->setAttribute(frameStamp->getFrameNumber(), "Rendering traversals end time ", endRenderingTraversals);
        getViewerStats()->setAttribute(frameStamp->getFrameNumber(), "Rendering traversals time taken", endRenderingTraversals-beginRenderingTraversals);
    }

    _requestRedraw = false;
}

