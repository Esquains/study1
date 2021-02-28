import torch
import numpy as np
from torch import nn

from module_def import SmartModule

############################## Preparation ##########################################################

class NeuralNet_All(nn.Module):
    def __init__(self, input_size, hidden_size, num_classes):
        super(NeuralNet_All, self).__init__()
        self.fc1 = nn.Linear(input_size, hidden_size)
        self.relu = nn.ReLU()
        self.fc2 = nn.Linear(hidden_size, num_classes)

    def forward(self, x):
        out = self.fc1(x)
        out = self.relu(out)
        out = self.fc2(out)
        out = self.relu(out)
        out = self.relu(out)

        return out

#####################################################################################################

total_input_size=5
total_hidden_size=4
total_num_classes=10
dummy_input = torch.randn(32, 5)

with torch.no_grad():
    my_model = NeuralNet_All(total_input_size, total_hidden_size, total_num_classes)
    my_model.eval()
    my_results = my_model(dummy_input)

    my_model = SmartModule(my_model)
    final_outputs = my_model(dummy_input)

    [np.testing.assert_allclose(my_results, final_outputs, rtol=1e-03, atol=1e-05)]
    print('===================== Results are expected ===========================')

print('End')
