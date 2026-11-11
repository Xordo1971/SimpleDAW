#pragma once
#include <juce_data_structures/juce_data_structures.h>

// Paso de deshacer genérico: el cambio ya se ha hecho cuando se registra,
// así que la primera "ejecución" no hace nada; después alterna deshacer / rehacer.
class LambdaUndoAction : public juce::UndoableAction
{
public:
    LambdaUndoAction (std::function<void()> undoFunction, std::function<void()> redoFunction)
        : undoFn (std::move (undoFunction)), redoFn (std::move (redoFunction)) {}

    bool perform() override
    {
        if (firstTime) { firstTime = false; return true; }
        if (redoFn) redoFn();
        return true;
    }

    bool undo() override
    {
        if (undoFn) undoFn();
        return true;
    }

private:
    std::function<void()> undoFn, redoFn;
    bool firstTime = true;
};
