/******************************************************************************
 *   Copyright (c)2012 Jan Rheinlaender <jrheinlaender@users.sourceforge.net> *
 *                                                                            *
 *   This file is part of the FreeCAD CAx development system.                 *
 *                                                                            *
 *   This library is free software; you can redistribute it and/or            *
 *   modify it under the terms of the GNU Library General Public              *
 *   License as published by the Free Software Foundation; either             *
 *   version 2 of the License, or (at your option) any later version.         *
 *                                                                            *
 *   This library  is distributed in the hope that it will be useful,         *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *   GNU Library General Public License for more details.                     *
 *                                                                            *
 *   You should have received a copy of the GNU Library General Public        *
 *   License along with this library; see the file COPYING.LIB. If not,       *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,            *
 *   Suite 330, Boston, MA  02111-1307, USA                                   *
 *                                                                            *
 ******************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
# include <QMessageBox>
# include <QListWidget>
# include <TopoDS_Shape.hxx>
# include <TopoDS_Face.hxx>
# include <TopoDS.hxx>
# include <BRepAdaptor_Surface.hxx>
#endif

#include <Base/Console.h>
#include <App/Application.h>
#include <App/Document.h>
#include <App/Part.h>
#include <App/Origin.h>
#include <App/Plane.h>
#include <App/Line.h>
#include <Gui/Application.h>
#include <Gui/Document.h>
#include <Gui/BitmapFactory.h>
#include <Gui/ViewProvider.h>
#include <Gui/WaitCursor.h>
#include <Gui/Selection.h>
#include <Gui/Command.h>

#include <Mod/PartDesign/App/FeatureTransformed.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/FeatureAddSub.h>

#include "ReferenceSelection.h"
#include "TaskMultiTransformParameters.h"
#include "Utils.h"

#include "TaskTransformedParameters.h"


using namespace PartDesignGui;
using namespace Gui;

/* TRANSLATOR PartDesignGui::TaskTransformedParameters */

TaskTransformedParameters::TaskTransformedParameters(ViewProviderTransformed *TransformedView, QWidget *parent)
    : TaskBox(Gui::BitmapFactory().pixmap((std::string("PartDesign_") + TransformedView->featureName).c_str()),
              QString::fromLatin1((TransformedView->featureName + " parameters").c_str()),
              true,
              parent),
      TransformedView(TransformedView),
      parentTask(NULL),
      insideMultiTransform(false),
      blockUpdate(false)
{
    selectionMode = none;
}

TaskTransformedParameters::TaskTransformedParameters(TaskMultiTransformParameters *parentTask)
    : TaskBox(QPixmap(), tr(""), true, parentTask),
      TransformedView(NULL),
      parentTask(parentTask),
      insideMultiTransform(true),
      blockUpdate(false)
{
    // Original feature selection makes no sense inside a MultiTransform
    selectionMode = none;
}

TaskTransformedParameters::~TaskTransformedParameters()
{
    // make sure to remove selection gate in all cases
    Gui::Selection().rmvSelectionGate();
}

bool TaskTransformedParameters::isViewUpdated() const
{
    return (blockUpdate == false);
}

int TaskTransformedParameters::getUpdateViewTimeout() const
{
    return 500;
}

const bool TaskTransformedParameters::originalSelected(const Gui::SelectionChanges& msg)
{
    if (msg.Type == Gui::SelectionChanges::AddSelection && (
                (selectionMode == addFeature) || (selectionMode == removeFeature))) {

        if (strcmp(msg.pDocName, getObject()->getDocument()->getName()) != 0)
            return false;

        PartDesign::Transformed* pcTransformed = getObject();
        App::DocumentObject* selectedObject = pcTransformed->getDocument()->getObject(msg.pObjectName);
        if (selectedObject->isDerivedFrom(PartDesign::FeatureAddSub::getClassTypeId())) {

            // Do the same like in TaskDlgTransformedParameters::accept() but without doCommand
            std::vector<App::DocumentObject*> originals = pcTransformed->Originals.getValues();
            std::vector<App::DocumentObject*>::iterator o = std::find(originals.begin(), originals.end(), selectedObject);
            if (selectionMode == addFeature) {
                if (o == originals.end())
                    originals.push_back(selectedObject);
                else
                    return false; // duplicate selection
            } else {
                if (o != originals.end())
                    originals.erase(o);
                else
                    return false;
            }
            pcTransformed->Originals.setValues(originals);
            recomputeFeature();

            return true;
        }
    }

    return false;
}

void TaskTransformedParameters::onButtonAddFeature(bool checked)
{
    if (checked) {
        hideObject();
        showBase();
        selectionMode = addFeature;
        Gui::Selection().clearSelection();
    } else {
        exitSelectionMode();
    }
}

void TaskTransformedParameters::onButtonRemoveFeature(bool checked)
{
    if (checked) {
        hideObject();
        showBase();
        selectionMode = removeFeature;
        Gui::Selection().clearSelection();
    } else {
        exitSelectionMode();
    }
}

void TaskTransformedParameters::removeItemFromListWidget(QListWidget* widget, const char* itemstr)
{
    QList<QListWidgetItem*> items = widget->findItems(QString::fromAscii(itemstr), Qt::MatchExactly);
    if (!items.empty()) {
        for (QList<QListWidgetItem*>::const_iterator i = items.begin(); i != items.end(); i++) {
            QListWidgetItem* it = widget->takeItem(widget->row(*i));
            delete it;
        }
    }
}


App::DocumentObject* TaskTransformedParameters::getPartPlanes(const char* str) const {
    //TODO: Adjust to GRAPH handling when available
    App::DocumentObject* obj = getObject();
    App::Part* part = getPartFor(obj, false);
    
    if (part) {
        std::vector<App::DocumentObject*> origs = part->getObjectsOfType(App::Origin::getClassTypeId());
        if(origs.size()<1)
            return nullptr;

        App::Origin* orig = static_cast<App::Origin*>(origs[0]);
        auto planes = orig->getObjectsOfType(App::Plane::getClassTypeId());
        for(App::DocumentObject* plane : planes) {
            if( strcmp(static_cast<App::Plane*>(plane)->PlaneType.getValue(), str) == 0)
                return plane;
        }
    }
    
    return nullptr;
}


App::DocumentObject* TaskTransformedParameters::getPartLines(const char* str) const {

    //TODO: Adjust to GRAPH handling when available
    App::DocumentObject* obj = getObject();
    App::Part* part = getPartFor(obj, false);
    if (part) {
        std::vector<App::DocumentObject*> origs = part->getObjectsOfType(App::Origin::getClassTypeId());
        if(origs.size()<1)
            return nullptr;

        App::Origin* orig = static_cast<App::Origin*>(origs[0]);
        auto lines = orig->getObjectsOfType(App::Line::getClassTypeId());
        for(App::DocumentObject* line : lines) {

            if( strcmp(static_cast<App::Line*>(line)->LineType.getValue(), str) == 0)
                return line;
        }
    }
    
    return nullptr;
}

void TaskTransformedParameters::fillAxisCombo(ComboLinks &combolinks,
                                              Part::Part2DObject* sketch)
{
    combolinks.clear();

    //add sketch axes
    if (sketch){
        combolinks.addLink(sketch, "N_Axis",tr("Normal sketch axis"));
        combolinks.addLink(sketch,"V_Axis",tr("Vertical sketch axis"));
        combolinks.addLink(sketch,"H_Axis",tr("Horizontal sketch axis"));
        for (int i=0; i < sketch->getAxisCount(); i++) {
            QString itemText = tr("Construction line %1").arg(i+1);
            std::stringstream sub;
            sub << "Axis" << i;
            combolinks.addLink(sketch,sub.str(),itemText);
        }
    }

    //add part axes
    App::DocumentObject* line = 0;
    line = getPartLines(App::Part::BaselineTypes[0]);
    if(line)
        combolinks.addLink(line,"",tr("Base X axis"));
    line = getPartLines(App::Part::BaselineTypes[1]);
    if(line)
        combolinks.addLink(line,"",tr("Base Y axis"));
    line = getPartLines(App::Part::BaselineTypes[2]);
    if(line)
        combolinks.addLink(line,"",tr("Base Z axis"));

    //add "Select reference"
    combolinks.addLink(0,std::string(),tr("Select reference..."));
}

void TaskTransformedParameters::fillPlanesCombo(ComboLinks &combolinks,
                                                Part::Part2DObject* sketch)
{
    combolinks.clear();

    //add sketch axes
    if (sketch){
        combolinks.addLink(sketch,"V_Axis",QObject::tr("Vertical sketch axis"));
        combolinks.addLink(sketch,"H_Axis",QObject::tr("Horizontal sketch axis"));
        for (int i=0; i < sketch->getAxisCount(); i++) {
            QString itemText = tr("Construction line %1").arg(i+1);
            std::stringstream sub;
            sub << "Axis" << i;
            combolinks.addLink(sketch,sub.str(),itemText);
        }
    }

    //add part baseplanes
    App::DocumentObject* plane = 0;
    plane = getPartPlanes(App::Part::BaseplaneTypes[0]);
    if(plane)
        combolinks.addLink(plane,"",tr("Base XY plane"));
    plane = getPartPlanes(App::Part::BaseplaneTypes[1]);
    if(plane)
        combolinks.addLink(plane,"",tr("Base XZ plane"));
    plane = getPartPlanes(App::Part::BaseplaneTypes[2]);
    if(plane)
        combolinks.addLink(plane,"",tr("Base YZ plane"));

    //add "Select reference"
    combolinks.addLink(0,std::string(),tr("Select reference..."));

}

void TaskTransformedParameters::recomputeFeature() {
    getTopTransformedView()->recomputeFeature();
}

PartDesignGui::ViewProviderTransformed *TaskTransformedParameters::getTopTransformedView() const {
    PartDesignGui::ViewProviderTransformed *rv;

    if (insideMultiTransform) {
        rv = parentTask->TransformedView;
    } else {
        rv = TransformedView;
    }
    assert (rv);

    return rv;
}

PartDesign::Transformed *TaskTransformedParameters::getTopTransformedObject() const {
	App::DocumentObject *transform = getTopTransformedView()->getObject();
	assert (transform->isDerivedFrom(PartDesign::Transformed::getClassTypeId()));
	return static_cast<PartDesign::Transformed*>(transform);
}


PartDesign::Transformed *TaskTransformedParameters::getObject() const {
    if (insideMultiTransform)
        return parentTask->getSubFeature();
    else
        return static_cast<PartDesign::Transformed*>(TransformedView->getObject());
}

Part::Feature *TaskTransformedParameters::getBaseObject() const {
    PartDesign::Feature* feature = getTopTransformedObject ();
    // NOTE: getBaseObject() throws if there is no base; shouldn't happen here.
    return feature->getBaseObject();
}

const std::vector<App::DocumentObject*> & TaskTransformedParameters::getOriginals(void) const {
    return getTopTransformedObject()->Originals.getValues();
}

App::DocumentObject* TaskTransformedParameters::getSketchObject() const {
    return getTopTransformedObject()->getSketchObject();
}

void TaskTransformedParameters::hideObject()
{
    Gui::Document* doc = Gui::Application::Instance->activeDocument();
    if (doc) {
        doc->setHide(getTopTransformedObject()->getNameInDocument());
    }
}

void TaskTransformedParameters::showObject()
{
    Gui::Document* doc = Gui::Application::Instance->activeDocument();
    if (doc) {
        doc->setShow(getTopTransformedObject()->getNameInDocument());
    }
}

void TaskTransformedParameters::hideBase()
{
    Gui::Document* doc = Gui::Application::Instance->activeDocument();
    if (doc) {
        try {
            doc->setHide(getBaseObject()->getNameInDocument());
        } catch (const Base::Exception &) { }
    }
}

void TaskTransformedParameters::showBase()
{
    Gui::Document* doc = Gui::Application::Instance->activeDocument();
    if (doc) {
        try {
            doc->setShow(getBaseObject()->getNameInDocument());
        } catch (const Base::Exception &) { }
    }
}

void TaskTransformedParameters::exitSelectionMode()
{
    clearButtons();
    selectionMode = none;
    Gui::Selection().rmvSelectionGate();
    showObject();
    hideBase();
}

void TaskTransformedParameters::addReferenceSelectionGate(bool edge, bool face)
{
    Gui::Selection().addSelectionGate(new ReferenceSelection(getBaseObject(), edge, face, /*point =*/ true));
}

//**************************************************************************
//**************************************************************************
// TaskDialog
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TaskDlgTransformedParameters::TaskDlgTransformedParameters(ViewProviderTransformed *TransformedView_)
    : TaskDlgFeatureParameters(TransformedView_)
{
    assert(vp);
    message = new TaskTransformedMessages(getTransformedView());

    Content.push_back(message);
}

//==== calls from the TaskView ===============================================================

bool TaskDlgTransformedParameters::accept()
{
    std::string name = vp->getObject()->getNameInDocument();

    //Gui::Command::openCommand(featureName + " changed");
    std::vector<App::DocumentObject*> originals = parameter->getOriginals();
    std::stringstream str;
    str << "App.ActiveDocument." << name.c_str() << ".Originals = [";
    for (std::vector<App::DocumentObject*>::const_iterator it = originals.begin(); it != originals.end(); ++it)
    {
        if ((*it) != NULL)
            str << "App.ActiveDocument." << (*it)->getNameInDocument() << ",";
    }
    str << "]";
    Gui::Command::runCommand(Gui::Command::Doc,str.str().c_str());

    // Continue (usually in virtual method accept())
    return TaskDlgFeatureParameters::accept ();
}

bool TaskDlgTransformedParameters::reject()
{
    // ensure that we are not in selection mode
    parameter->exitSelectionMode();

    return TaskDlgFeatureParameters::reject ();
}


#include "moc_TaskTransformedParameters.cpp"


ComboLinks::ComboLinks(QComboBox &combo)
    : doc(0)
{
    this->_combo = &combo;
    _combo->clear();
}

int ComboLinks::addLink(const App::PropertyLinkSub &lnk, QString itemText)
{
    if(!_combo)
        return 0;
    _combo->addItem(itemText);
    this->linksInList.push_back(new App::PropertyLinkSub());
    App::PropertyLinkSub &newitem = *(linksInList[linksInList.size()-1]);
    newitem.Paste(lnk);
    if (newitem.getValue() && this->doc == 0)
        this->doc = newitem.getValue()->getDocument();
    return linksInList.size()-1;
}

int ComboLinks::addLink(App::DocumentObject *linkObj, std::string linkSubname, QString itemText)
{
    if(!_combo)
        return 0;
    _combo->addItem(itemText);
    this->linksInList.push_back(new App::PropertyLinkSub());
    App::PropertyLinkSub &newitem = *(linksInList[linksInList.size()-1]);
    newitem.setValue(linkObj,std::vector<std::string>(1,linkSubname));
    if (newitem.getValue() && this->doc == 0)
        this->doc = newitem.getValue()->getDocument();
    return linksInList.size()-1;
}

void ComboLinks::clear()
{
    for(int i = 0  ;  i < this->linksInList.size()  ;  i++){
        delete linksInList[i];
    }
    if(this->_combo)
        _combo->clear();
}

App::PropertyLinkSub &ComboLinks::getLink(int index) const
{
    if (index < 0 || index > linksInList.size()-1)
        throw Base::Exception("ComboLinks::getLink:Index out of range");
    if (linksInList[index]->getValue() && doc && !(doc->isIn(linksInList[index]->getValue())))
        throw Base::Exception("Linked object is not in the document; it may have been deleted");
    return *(linksInList[index]);
}

App::PropertyLinkSub &ComboLinks::getCurrentLink() const
{
    assert(_combo);
    return getLink(_combo->currentIndex());
}

int ComboLinks::setCurrentLink(const App::PropertyLinkSub &lnk)
{
    for(int i = 0  ;  i < linksInList.size()  ;  i++) {
        App::PropertyLinkSub &it = *(linksInList[i]);
        if(lnk.getValue() == it.getValue() && lnk.getSubValues() == it.getSubValues()){
            bool wasBlocked = _combo->signalsBlocked();
            _combo->blockSignals(true);
            _combo->setCurrentIndex(i);
            _combo->blockSignals(wasBlocked);
            return i;
        }
    }
    return -1;
}
