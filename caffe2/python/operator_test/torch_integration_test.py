from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

from caffe2.python import core, workspace
import torch
from hypothesis import given
import caffe2.python.hypothesis_test_util as hu
import hypothesis.strategies as st
import numpy as np
from scipy import interpolate

def generate_rois(roi_counts, im_dims):
    assert len(roi_counts) == len(im_dims)
    all_rois = []
    for i, num_rois in enumerate(roi_counts):
        if num_rois == 0:
            continue
        # [batch_idx, x1, y1, x2, y2]
        rois = np.random.uniform(0, im_dims[i], size=(roi_counts[i], 5)).astype(
            np.float32
        )
        rois[:, 0] = i  # batch_idx
        # Swap (x1, x2) if x1 > x2
        rois[:, 1], rois[:, 3] = (
            np.minimum(rois[:, 1], rois[:, 3]),
            np.maximum(rois[:, 1], rois[:, 3]),
        )
        # Swap (y1, y2) if y1 > y2
        rois[:, 2], rois[:, 4] = (
            np.minimum(rois[:, 2], rois[:, 4]),
            np.maximum(rois[:, 2], rois[:, 4]),
        )
        all_rois.append(rois)
    if len(all_rois) > 0:
        return np.vstack(all_rois)
    return np.empty((0, 5)).astype(np.float32)

def generate_rois_rotated(roi_counts, im_dims):
    rois = generate_rois(roi_counts, im_dims)
    # [batch_id, ctr_x, ctr_y, w, h, angle]
    rotated_rois = np.empty((rois.shape[0], 6)).astype(np.float32)
    rotated_rois[:, 0] = rois[:, 0]  # batch_id
    rotated_rois[:, 1] = (rois[:, 1] + rois[:, 3]) / 2.  # ctr_x = (x1 + x2) / 2
    rotated_rois[:, 2] = (rois[:, 2] + rois[:, 4]) / 2.  # ctr_y = (y1 + y2) / 2
    rotated_rois[:, 3] = rois[:, 3] - rois[:, 1] + 1.0  # w = x2 - x1 + 1
    rotated_rois[:, 4] = rois[:, 4] - rois[:, 2] + 1.0  # h = y2 - y1 + 1
    rotated_rois[:, 5] = np.random.uniform(-90.0, 90.0)  # angle in degrees
    return rotated_rois

def gen_boxes(count, center):
    len = 10
    len_half = len / 2.0
    ret = np.tile(
        np.array(
            [center[0] - len_half, center[1] - len_half,
            center[0] + len_half, center[1] + len_half]
        ).astype(np.float32),
        (count, 1)
    )
    return ret

def gen_multiple_boxes(centers, scores, count, num_classes):
    ret_box = None
    ret_scores = None
    for cc, ss in zip(centers, scores):
        box = gen_boxes(count, cc)
        ret_box = np.vstack((ret_box, box)) if ret_box is not None else box
        cur_sc = np.ones((count, 1), dtype=np.float32) * ss
        ret_scores = np.vstack((ret_scores, cur_sc)) \
            if ret_scores is not None else cur_sc
    ret_box = np.tile(ret_box, (1, num_classes))
    ret_scores = np.tile(ret_scores, (1, num_classes))
    assert ret_box.shape == (len(centers) * count, 4 * num_classes)
    assert ret_scores.shape == (len(centers) * count, num_classes)
    return ret_box, ret_scores


class TorchIntegration(hu.HypothesisTestCase):
    @given(
        H=st.integers(min_value=50, max_value=100),
        W=st.integers(min_value=50, max_value=100),
        C=st.integers(min_value=1, max_value=3),
        num_rois=st.integers(min_value=1, max_value=10),
        pooled_size=st.sampled_from([7, 14])
        )
    def test_roi_align(self, H, W, C, num_rois, pooled_size):
        X = np.random.randn(1, C, H, W).astype(np.float32)
        R = np.zeros((num_rois, 5)).astype(np.float32)
        for i in range(num_rois):
            x = np.random.uniform(1, W - 1)
            y = np.random.uniform(1, H - 1)
            w = np.random.uniform(1, min(x, W - x))
            h = np.random.uniform(1, min(y, H - y))
            R[i] = [0, x, y, w, h]

        def roialign_ref(X, R):
            ref_op = core.CreateOperator(
                "RoIAlign",
                ["X_ref", "R_ref"],
                ["Y_ref"],
                order="NCHW",
                spatial_scale=1.0,
                pooled_h=pooled_size,
                pooled_w=pooled_size,
                sampling_ratio=0,
            )
            workspace.FeedBlob("X_ref", X)
            workspace.FeedBlob("R_ref", R)
            workspace.RunOperatorOnce(ref_op)
            return workspace.FetchBlob("Y_ref")
        Y_ref = torch.tensor(roialign_ref(X, R))
        Y = torch.ops._caffe2.RoIAlign(
                torch.tensor(X), torch.tensor(R),
                "NCHW", 1., pooled_size, pooled_size, 0)
        torch.testing.assert_allclose(Y, Y_ref)

    @given(
        A=st.integers(min_value=4, max_value=4),
        H=st.integers(min_value=10, max_value=10),
        W=st.integers(min_value=8, max_value=8),
        img_count=st.integers(min_value=3, max_value=3),
        )
    def test_generate_proposals(self, A, H, W, img_count):
        scores = np.ones((img_count, A, H, W)).astype(np.float32)
        bbox_deltas = np.linspace(0, 10, num=img_count*4*A*H*W).reshape(
                (img_count, 4*A, H, W)).astype(np.float32)
        im_info = np.ones((img_count, 3)).astype(np.float32) / 10
        anchors = np.ones((A, 4)).astype(np.float32)

        def generate_proposals_ref():
            ref_op = core.CreateOperator(
                "GenerateProposals",
                ["scores", "bbox_deltas", "im_info", "anchors"],
                ["rois", "rois_probs"],
                spatial_scale=2.0,
            )
            workspace.FeedBlob("scores", scores)
            workspace.FeedBlob("bbox_deltas", bbox_deltas)
            workspace.FeedBlob("im_info", im_info)
            workspace.FeedBlob("anchors", anchors)
            workspace.RunOperatorOnce(ref_op)
            return workspace.FetchBlob("rois"), workspace.FetchBlob("rois_probs")

        rois, rois_probs = generate_proposals_ref()
        rois = torch.tensor(rois)
        rois_probs = torch.tensor(rois_probs)
        a, b = torch.ops._caffe2.GenerateProposals(
                torch.tensor(scores), torch.tensor(bbox_deltas),
                torch.tensor(im_info), torch.tensor(anchors),
                2.0, 6000, 300, 0.7, 16, False, True, -90, 90, 1.0)
        torch.testing.assert_allclose(rois, a)
        torch.testing.assert_allclose(rois_probs, b)

    @given(
        roi_counts=st.lists(st.integers(0, 5), min_size=1, max_size=10),
        num_classes=st.integers(1, 10),
        rotated=st.booleans(),
        angle_bound_on=st.booleans(),
        clip_angle_thresh=st.sampled_from([-1.0, 1.0]),
        **hu.gcs_cpu_only
    )
    def test_bbox_transform(self,
        roi_counts,
        num_classes,
        rotated,
        angle_bound_on,
        clip_angle_thresh,
        gc,
        dc,
            ):
        """
        Test with rois for multiple images in a batch
        """
        batch_size = len(roi_counts)
        total_rois = sum(roi_counts)
        im_dims = np.random.randint(100, 600, batch_size)
        rois = (
            generate_rois_rotated(roi_counts, im_dims)
            if rotated
            else generate_rois(roi_counts, im_dims)
        )
        box_dim = 5 if rotated else 4
        deltas = np.random.randn(total_rois, box_dim * num_classes).astype(np.float32)
        im_info = np.zeros((batch_size, 3)).astype(np.float32)
        im_info[:, 0] = im_dims
        im_info[:, 1] = im_dims
        im_info[:, 2] = 1.0

        def bbox_transform_ref():
            ref_op = core.CreateOperator(
                "BBoxTransform",
                ["rois", "deltas", "im_info"],
                ["box_out"],
                apply_scale=False,
                correct_transform_coords=True,
                rotated=rotated,
                angle_bound_on=angle_bound_on,
                clip_angle_thresh=clip_angle_thresh,
            )
            workspace.FeedBlob("rois", rois)
            workspace.FeedBlob("deltas", deltas)
            workspace.FeedBlob("im_info", im_info)
            workspace.RunOperatorOnce(ref_op)
            return workspace.FetchBlob("box_out")

        box_out = torch.tensor(bbox_transform_ref())
        a, b = torch.ops._caffe2.BBoxTransform(
                torch.tensor(rois), torch.tensor(deltas),
                torch.tensor(im_info),
                [1.0, 1.0, 1.0, 1.0],
                False, True, rotated, angle_bound_on,
                -90, 90, clip_angle_thresh)

        torch.testing.assert_allclose(box_out, a)


    @given(
            det_per_im=st.integers(1, 3),
            num_classes=st.integers(2, 10)
        )
    def test_box_with_nms_limit(self,
            det_per_im,
            num_classes,
            ):

        in_centers = [(0, 0), (20, 20), (50, 50)]
        in_scores = [0.7, 0.85, 0.6]
        boxes, scores = gen_multiple_boxes(in_centers, in_scores, 10, num_classes)

        def box_with_nms_limit_ref():
            ref_op = core.CreateOperator(
                "BoxWithNMSLimit",
                ['in_scores', 'in_boxes'],# 'in_batch_splits'],
                ['scores', 'boxes', 'classes'],# 'batch_splits', 'keeps', 'keeps_size'],
                score_thresh = 0.5,
                nms=0.9,
                detections_per_im=det_per_im,
            )
            workspace.FeedBlob("in_scores", scores)
            workspace.FeedBlob("in_boxes", boxes)
            workspace.RunOperatorOnce(ref_op)
            return workspace.FetchBlob("scores"), workspace.FetchBlob("boxes"), workspace.FetchBlob("classes")

        scores_ref, boxes_ref, classes_ref = box_with_nms_limit_ref()

        a, b, c = torch.ops._caffe2.BoxWithNMSLimit(
                torch.tensor(scores), torch.tensor(boxes),
                0.5, 0.9, det_per_im, False, "linear", 0.5, 0.1, False)
        torch.testing.assert_allclose(torch.tensor(scores_ref), a)
        torch.testing.assert_allclose(torch.tensor(boxes_ref), b)
        torch.testing.assert_allclose(torch.tensor(classes_ref), c)


    def test_heatmap_max_keypoint(self):
        NUM_TEST_ROI = 14
        NUM_KEYPOINTS = 19
        HEATMAP_SIZE = 56
        np.random.seed(0)

        # initial coordinates and interpolate HEATMAP_SIZE from it
        HEATMAP_SMALL_SIZE = 4
        bboxes_in = 500 * np.random.rand(NUM_TEST_ROI, 4).astype(np.float32)
        # only bbox with smaller first coordiantes
        for i in range(NUM_TEST_ROI):
            if bboxes_in[i][0] > bboxes_in[i][2]:
                tmp = bboxes_in[i][2]
                bboxes_in[i][2] = bboxes_in[i][0]
                bboxes_in[i][0] = tmp
            if bboxes_in[i][1] > bboxes_in[i][3]:
                tmp = bboxes_in[i][3]
                bboxes_in[i][3] = bboxes_in[i][1]
                bboxes_in[i][1] = tmp

        # initial randomized coordiantes for heatmaps and expand it with interpolation
        init = np.random.rand(
            NUM_TEST_ROI,
            NUM_KEYPOINTS,
            HEATMAP_SMALL_SIZE,
            HEATMAP_SMALL_SIZE).astype(np.float32)
        heatmaps_in = np.zeros((NUM_TEST_ROI, NUM_KEYPOINTS,
            HEATMAP_SIZE, HEATMAP_SIZE)).astype(np.float32)
        for roi in range(NUM_TEST_ROI):
            for keyp in range(NUM_KEYPOINTS):
                f = interpolate.interp2d(
                    np.arange(0, 1, 1.0 / HEATMAP_SMALL_SIZE),
                    np.arange(0, 1, 1.0 / HEATMAP_SMALL_SIZE),
                    init[roi][keyp],
                    kind='cubic')
                heatmaps_in[roi][keyp] = f(
                    np.arange(0, 1, 1.0 / HEATMAP_SIZE),
                    np.arange(0, 1, 1.0 / HEATMAP_SIZE))
        def heatmap_max_keypoint_ref():
            ref_op = core.CreateOperator(
                    'HeatmapMaxKeypoint',
                    ['heatmaps_in', 'bboxes_in'],
                    ['keypoints_out'],
                    should_output_softmax = True,
                    )
            workspace.FeedBlob("heatmaps_in", heatmaps_in)
            workspace.FeedBlob("bboxes_in", bboxes_in)
            workspace.RunOperatorOnce(ref_op)
            return workspace.FetchBlob("keypoints_out")

        keypoints_ref = heatmap_max_keypoint_ref()

        keypoints_torch = torch.ops._caffe2.HeatmapMaxKeypoint(
                torch.tensor(heatmaps_in), torch.tensor(bboxes_in),
                True)
        torch.testing.assert_allclose(torch.tensor(keypoints_ref), keypoints_torch)
