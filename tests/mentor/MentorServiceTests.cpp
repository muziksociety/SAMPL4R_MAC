#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include "../../src/app/EventBus.h"
#include "../../src/mentor/MentorService.h"

class MentorServiceTests : public juce::UnitTest {
public:
    MentorServiceTests() : juce::UnitTest("MentorServiceTests") {}

    void runTest() override {
        // Ensure a clean slate for persistence tests
        {
            juce::PropertiesFile::Options o;
            o.applicationName     = "Samplar";
            o.filenameSuffix      = ".settings";
            o.folderName          = "Samplar";
            o.osxLibrarySubFolder = "Application Support";
            juce::PropertiesFile p(o);
            p.removeValue("mentor.completedLessons");
            p.saveIfNeeded();
        }

        beginTest("Section_ids_all_present");
        {
            samplar::EventBus bus;
            samplar::MentorService svc(bus);
            svc.initialise();
            const auto& sections = svc.getSections();
            expectEquals(sections.size(), 4);
            expect(svc.getSection("fullsong") != nullptr);
            expect(svc.getSection("drumstem") != nullptr);
            expect(svc.getSection("smode")    != nullptr);
            expect(svc.getSection("drumlib")  != nullptr);
            svc.shutdown();
        }

        beginTest("nextStep_prevStep_bounds");
        {
            samplar::EventBus bus;
            samplar::MentorService svc(bus);
            svc.initialise();
            svc.openLesson("fullsong", "fullsong_intro");
            expect(svc.getCurrentStep() != nullptr);
            expectEquals(svc.currentStepIndex(), 0);
            svc.prevStep();
            expectEquals(svc.currentStepIndex(), 0); // no underflow
            svc.nextStep(); svc.nextStep();
            expectEquals(svc.currentStepIndex(), 2);
            svc.nextStep(); // past last step -> marks completed, index stays
            expect(svc.isLessonCompleted("fullsong_intro"));
            svc.shutdown();
        }

        beginTest("completed_lessons_persist_round_trip");
        {
            samplar::EventBus bus;
            {
                samplar::MentorService a(bus);
                a.initialise();
                a.openLesson("smode", "smode_intro");
                a.nextStep(); a.nextStep(); // past last step -> completes
                expect(a.isLessonCompleted("smode_intro"));
                a.shutdown();
            }
            {
                samplar::MentorService b(bus);
                b.initialise();
                expect(b.isLessonCompleted("smode_intro"));
                b.shutdown();
            }
        }
    }
};

static MentorServiceTests mentorServiceTests;
